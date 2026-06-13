#!/usr/bin/env python3
"""
MODULE:    p.in.lroc.nac
AUTHOR:    Yann Chemin <dr.yann.chemin@gmail.com>
PURPOSE:   List and import LROC NAC DTMs from the ASU LROC PDS3/PDS4
           archive (pds.lroc.asu.edu / pds.lroc.im-ldi.com), filtering by
           lat/lon bounding box or by product-name regex. DTMs are served
           as GeoTIFFs and imported via r.in.gdal.
LICENSE:   The Unlicense (https://unlicense.org)
"""

# %module
# % description: Fetch and import LROC NAC DTMs from the ASU LROC archive, filtered by bbox or name.
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: raster
# % keyword: PDS3
# % keyword: PDS4
# % keyword: LROC
# % keyword: NAC
# % keyword: Moon
# % keyword: download
# %end

# %option
# % key: bbox
# % type: string
# % required: no
# % label: Bounding box W,S,E,N in degrees (longitudes 0..360 east-positive)
# % description: Filter products by lat/lon intersection. Polar studies typically use 0,-90,360,-80 or similar full-longitude windows.
# %end

# %option
# % key: name
# % type: string
# % required: no
# % label: Product name or substring (case-insensitive), e.g. NOBILE, MALAPERT01, MOUTON
# % description: Filters products whose name contains this substring. Combined with bbox by AND.
# %end

# %option G_OPT_R_OUTPUT
# % required: no
# % description: GRASS raster name for the imported DTM. Required unless -l is given.
# %end

# %option
# % key: download_dir
# % type: string
# % required: no
# % description: Directory for cached downloads (default: $HOME/.cache/p_in_lroc_nac/data)
# %end

# %option
# % key: cache_index
# % type: string
# % required: no
# % description: Path to the local product index JSON (default: $HOME/.cache/p_in_lroc_nac/index.json)
# %end

# %option
# % key: limit
# % type: integer
# % required: no
# % answer: 20
# % description: Maximum number of listed matches (use with -l)
# %end

# %option
# % key: workers
# % type: integer
# % required: no
# % answer: 16
# % description: Parallel HTTP workers when building the index
# %end

# %flag
# % key: l
# % description: List matching products and exit without downloading or importing
# %end

# %flag
# % key: r
# % description: Refresh (rebuild) the local product index from the remote archive
# %end

# %flag
# % key: k
# % description: Keep downloaded files after import (do not delete)
# %end

# %flag
# % key: d
# % description: Download only — do not import into GRASS. Useful for caching files for later sync to another machine or for projects in a different CRS.
# %end

# %flag
# % key: o
# % description: Pass -o to r.in.gdal (override CRS mismatch). Use when you know the source projection matches the active project's intent.
# %end

# %rules
# % required: bbox, name, -r
# % exclusive: -l, output
# % exclusive: -d, output
# %end

import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_meta


# ── Constants ───────────────────────────────────────────────────────────────
# The pds.lroc.asu.edu host 301-redirects to pds.lroc.im-ldi.com; we hit the
# canonical mirror directly to avoid the extra round-trip per request.
ARCHIVE_BASE = ("https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/"
                "LROLRC_2001/DATA/SDP/NAC_DTM")
DEFAULT_CACHE_DIR = os.path.expanduser("~/.cache/p_in_lroc_nac")
DEFAULT_INDEX_PATH = os.path.join(DEFAULT_CACHE_DIR, "index.json")
DEFAULT_DOWNLOAD_DIR = os.path.join(DEFAULT_CACHE_DIR, "data")
USER_AGENT = "p.in.lroc.nac/0.1 (GRASS GIS addon)"
HTTP_TIMEOUT = 60

# PDS4 namespace used by the cart:* bounding-coordinate elements.
CART_NS = "http://pds.nasa.gov/pds4/cart/v1"


# ── HTTP helpers ────────────────────────────────────────────────────────────

def _http_get(url, timeout=HTTP_TIMEOUT):
    """GET *url* and return the response body as bytes. Raises on error."""
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def _http_download(url, dst_path, timeout=HTTP_TIMEOUT):
    """Stream *url* to *dst_path*. Returns the number of bytes written."""
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    nbytes = 0
    tmp = dst_path + ".part"
    with urllib.request.urlopen(req, timeout=timeout) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            nbytes += len(chunk)
    os.replace(tmp, dst_path)
    return nbytes


# ── Archive scraping ────────────────────────────────────────────────────────

_HREF_RE = re.compile(r'href="([^"?/][^"]*)/"', re.IGNORECASE)


def list_products():
    """Return the list of NAC DTM product names from the archive listing."""
    html = _http_get(ARCHIVE_BASE + "/").decode("utf-8", errors="replace")
    return sorted(set(_HREF_RE.findall(html)))


def fetch_bbox(product):
    """
    Fetch the PDS4 .xml label for *product* and return
    (W, S, E, N) in degrees (east-positive longitudes), or None on failure.
    """
    url = f"{ARCHIVE_BASE}/{product}/NAC_DTM_{product}.xml"
    try:
        xml = _http_get(url, timeout=30)
    except (urllib.error.URLError, urllib.error.HTTPError):
        return None
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return None
    # Find Bounding_Coordinates anywhere in the tree.
    bc = root.find(f".//{{{CART_NS}}}Bounding_Coordinates")
    if bc is None:
        return None

    def _val(tag):
        e = bc.find(f"{{{CART_NS}}}{tag}")
        return float(e.text) if e is not None and e.text else None

    w = _val("west_bounding_coordinate")
    e = _val("east_bounding_coordinate")
    n = _val("north_bounding_coordinate")
    s = _val("south_bounding_coordinate")
    if None in (w, s, e, n):
        return None
    return (w, s, e, n)


def build_index(workers=16, progress_every=50):
    """
    Scrape the NAC DTM directory and fetch the bbox of every product.
    Returns a dict: {product_name: {"bbox": [w, s, e, n], "url_tif": "..."}}.
    Products whose XML cannot be parsed are recorded with bbox=None so they
    are not retried on the next run unless -r is passed again.
    """
    names = list_products()
    gs.message(f"Indexing {len(names)} NAC DTM products "
               f"with {workers} HTTP workers…")
    out = {}
    done = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        fut_to_name = {pool.submit(fetch_bbox, n): n for n in names}
        for fut in as_completed(fut_to_name):
            name = fut_to_name[fut]
            try:
                bbox = fut.result()
            except Exception:
                bbox = None
            out[name] = {
                "bbox":    list(bbox) if bbox else None,
                "url_tif": f"{ARCHIVE_BASE}/{name}/NAC_DTM_{name}.TIF",
                "url_lbl": f"{ARCHIVE_BASE}/{name}/NAC_DTM_{name}.LBL",
                "url_xml": f"{ARCHIVE_BASE}/{name}/NAC_DTM_{name}.xml",
            }
            done += 1
            if done % progress_every == 0:
                gs.percent(done, len(names), 1)
    gs.percent(1, 1, 1)
    return out


def load_or_build_index(index_path, refresh, workers):
    if not refresh and os.path.isfile(index_path):
        try:
            with open(index_path) as f:
                return json.load(f)
        except (OSError, ValueError) as e:
            gs.warning(f"Could not read index {index_path}: {e}; rebuilding.")
    index = build_index(workers=workers)
    os.makedirs(os.path.dirname(index_path), exist_ok=True)
    with open(index_path, "w") as f:
        json.dump(index, f, indent=2)
    gs.message(f"Index written: {index_path} "
               f"({sum(1 for v in index.values() if v['bbox'])} with bbox)")
    return index


# ── Querying ────────────────────────────────────────────────────────────────

def _parse_bbox(spec):
    """Parse 'W,S,E,N' into a tuple of floats; raise on malformed input."""
    parts = [p.strip() for p in spec.split(",")]
    if len(parts) != 4:
        raise ValueError("bbox must be 'W,S,E,N'")
    w, s, e, n = (float(p) for p in parts)
    if s >= n:
        raise ValueError("bbox south must be less than north")
    return (w, s, e, n)


def _lon_intersect(a_w, a_e, b_w, b_e):
    """
    1-D longitude intersection allowing for products and queries that
    cross the dateline (encoded by w > e). Returns True on any overlap.
    """
    def _spans(w, e):
        # Normalise to a list of (w, e) intervals that don't wrap.
        if w <= e:
            return [(w, e)]
        return [(w, 360.0), (0.0, e)]
    for aw, ae in _spans(a_w, a_e):
        for bw, be in _spans(b_w, b_e):
            if aw <= be and bw <= ae:
                return True
    return False


def filter_index(index, bbox=None, name=None):
    """Return a list of (product, entry) matching bbox and/or name."""
    name_re = re.compile(re.escape(name), re.IGNORECASE) if name else None
    out = []
    for prod, entry in sorted(index.items()):
        if name_re and not name_re.search(prod):
            continue
        if bbox is not None:
            if not entry.get("bbox"):
                continue
            pw, ps, pe, pn = entry["bbox"]
            qw, qs, qe, qn = bbox
            if pn < qs or ps > qn:
                continue
            if not _lon_intersect(pw, pe, qw, qe):
                continue
        out.append((prod, entry))
    return out


# ── Download + import ───────────────────────────────────────────────────────

def download_product(product, entry, download_dir):
    """Download .TIF/.LBL/.xml triplet into download_dir/<product>/."""
    pdir = os.path.join(download_dir, product)
    os.makedirs(pdir, exist_ok=True)
    paths = {}
    for kind in ("tif", "lbl", "xml"):
        url = entry[f"url_{kind}"]
        dst = os.path.join(pdir, os.path.basename(url))
        if not os.path.isfile(dst):
            gs.message(f"  fetching {url}")
            _http_download(url, dst)
        paths[kind] = dst
    return paths


def import_tif(tif_path, output, keep, override_crs=False):
    gs.message(f"Importing {os.path.basename(tif_path)} as <{output}>…")
    kwargs = dict(input=tif_path, output=output, overwrite=True)
    if override_crs:
        kwargs["flags"] = "o"
    gs.run_command("r.in.gdal", **kwargs)
    if not keep:
        try:
            os.remove(tif_path)
        except OSError:
            pass


# ── Pretty printing ─────────────────────────────────────────────────────────

def _print_matches(matches, limit):
    if not matches:
        gs.message("No matching products.")
        return
    gs.message(f"Found {len(matches)} matching products "
               f"(showing up to {limit}):")
    gs.message("  PRODUCT          W       S       E       N")
    gs.message("  ---------------- ------- ------- ------- -------")
    for prod, entry in matches[:limit]:
        bb = entry.get("bbox")
        if bb:
            w, s, e, n = bb
            gs.message(f"  {prod:<16} {w:7.2f} {s:7.2f} {e:7.2f} {n:7.2f}")
        else:
            gs.message(f"  {prod:<16} (bbox unavailable)")


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    opt_bbox      = options["bbox"]
    opt_name      = options["name"]
    opt_output    = options["output"]
    opt_dl_dir    = options["download_dir"] or DEFAULT_DOWNLOAD_DIR
    opt_index     = options["cache_index"] or DEFAULT_INDEX_PATH
    opt_limit     = int(options["limit"])
    opt_workers   = int(options["workers"])
    flag_list     = flags["l"]
    flag_refresh  = flags["r"]
    flag_keep     = flags["k"]
    flag_download = flags["d"]
    flag_override = flags["o"]

    bbox = None
    if opt_bbox:
        try:
            bbox = _parse_bbox(opt_bbox)
        except ValueError as e:
            gs.fatal(str(e))

    index = load_or_build_index(opt_index, refresh=flag_refresh,
                                workers=opt_workers)

    if flag_refresh and not (opt_bbox or opt_name or flag_list or opt_output):
        return

    matches = filter_index(index, bbox=bbox, name=opt_name or None)

    if flag_list or (not opt_output and not flag_download):
        _print_matches(matches, opt_limit)
        return

    if not matches:
        gs.fatal("No products match the given bbox/name filters.")

    # Download-only mode: fetch all matches (no GRASS import).
    if flag_download:
        os.makedirs(opt_dl_dir, exist_ok=True)
        for product, entry in matches:
            gs.message(f"→ {product}")
            download_product(product, entry, opt_dl_dir)
        gs.message(f"Done. {len(matches)} product(s) cached under "
                   f"{opt_dl_dir}.")
        return

    if len(matches) > 1:
        names = ", ".join(p for p, _ in matches[:10])
        gs.fatal(
            f"{len(matches)} products match the filters ({names}…). "
            "Narrow the bbox/name to select exactly one, use -l to list, "
            "or use -d to download all without importing.")

    product, entry = matches[0]
    if not entry.get("bbox"):
        gs.warning(f"Product {product} has no parsed bbox; importing anyway.")
    os.makedirs(opt_dl_dir, exist_ok=True)
    paths = download_product(product, entry, opt_dl_dir)
    import_tif(paths["tif"], opt_output, keep=flag_keep,
               override_crs=flag_override)
    p_meta.write_planetary_metadata(
        opt_output,
        module="p.in.lroc.nac",
        command=" ".join(sys.argv),
        data_type="dem",
        mission="LRO",
        sensor="LROC_NAC",
        body="MOON",
        radiometric_quantity="elevation",
        radiometric_units="m",
        pds_product_id=product,
        source_file=paths["tif"],
    )
    gs.message(f"Done. Imported {product} → <{opt_output}>.")


if __name__ == "__main__":
    options, flags = gs.parser()
    sys.exit(main() or 0)
