#!/usr/bin/env python3
"""
MODULE:    p.in.astropedia
AUTHOR:    Yann Chemin <dr.yann.chemin@gmail.com>
PURPOSE:   Fetch and import planetary data products from the USGS Astropedia
           STAC catalog, the NASA PDS Federated Search API, or the PDS
           Ring-Moon Systems Node OPUS search interface. Supports DOI
           resolution, PDS4 LID lookup, keyword search, curated USGS COG
           mosaics, and direct OPUS observation queries (including Cassini
           VIMS hyperspectral cubes). Downloaded files are imported via
           r.in.gdal, p.in.pds4, or p.in.pds3 depending on file type.
LICENSE:   The Unlicense (https://unlicense.org)
           This is free and unencumbered software released into the public domain.
"""

# %module
# % description: Fetch and import planetary data from USGS Astropedia, NASA PDS, or OPUS (Ring-Moon Systems Node). Supports VIMS hyperspectral cubes via opus= or opus_id=.
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: raster
# % keyword: PDS4
# % keyword: Astropedia
# % keyword: VIMS
# % keyword: OPUS
# % keyword: download
# %end

# %option
# % key: doi
# % type: string
# % required: no
# % multiple: no
# % description: Dataset DOI to resolve (e.g. 10.17189/1519101 or full https://doi.org/ URL)
# %end

# %option
# % key: lid
# % type: string
# % required: no
# % multiple: no
# % description: PDS4 Logical Identifier (LID), e.g. urn:nasa:pds:mgs-mola-dem-mars:data:megt90n000cb
# %end

# %option
# % key: search
# % type: string
# % required: no
# % multiple: no
# % description: Free-text keyword search against the USGS Astropedia STAC catalog
# %end

# %option
# % key: cog
# % type: string
# % required: no
# % multiple: no
# % description: USGS planetarymaps.usgs.gov COG/GeoTIFF mosaic: a catalog key (see -l) or a direct https URL. Imported via /vsicurl/, windowed to the active region.
# %end

# %option
# % key: opus
# % type: string
# % required: no
# % multiple: no
# % label: OPUS (Ring-Moon Systems Node) search query
# % description: Comma-separated key=value OPUS API parameters, e.g. instrument=Cassini VIMS,target=Saturn. Use -l to list matching observations without downloading.
# %end

# %option
# % key: opus_id
# % type: string
# % required: no
# % multiple: no
# % label: OPUS observation ID to download directly
# % description: Download and import a specific OPUS observation, e.g. co-vims-v1590123456. Skips the search step.
# %end

# %option
# % key: vims_channel
# % type: string
# % required: no
# % options: vis,ir
# % answer: vis
# % label: VIMS channel to import
# % description: vis: VIS channel (0.35-1.05 µm, 96 bands); ir: IR channel (0.88-5.1 µm, 256 bands). Applies to VIMS .qub cubes fetched via opus= or opus_id=.
# %end

# %option G_OPT_R_OUTPUT
# % required: no
# % description: GRASS raster name for the imported product. Required unless -l is given.
# %end

# %option
# % key: band
# % type: integer
# % required: no
# % answer: 1
# % description: Band index to import for multi-band products (default: 1)
# %end

# %option
# % key: limit
# % type: integer
# % required: no
# % answer: 10
# % description: Maximum number of results shown by -l or considered for download
# %end

# %option
# % key: download_dir
# % type: string
# % required: no
# % description: Directory for cached downloads (default: system temp dir)
# %end

# %flag
# % key: l
# % description: List matching products and exit without downloading or importing
# %end

# %flag
# % key: r
# % description: Ignore the active GRASS region; search globally regardless of current window
# %end

# %flag
# % key: k
# % description: Keep downloaded source file after import (do not delete)
# %end

# %flag
# % key: o
# % description: Override projection check (passed as -o to r.in.gdal / p.in.pds4)
# %end

# %option
# % key: project
# % type: string
# % required: no
# % label: Name of GRASS project to create (or switch into) at the dataset's native CRS
# % description: When set, p.in.astropedia probes the source dataset's CRS (via gdalsrsinfo for /vsicurl/ URLs, or from the local PDS4 label) and either reuses an existing project of that name OR creates one with the matching projection via g.proj. The current GRASS session is then switched into the new project before the raster is imported, eliminating the CRS-mismatch / reproject step that otherwise bites users importing PDS-native products (e.g. Mars MOLA in Equirectangular Mars) into a non-matching working project. The original project is restored at exit.
# %end

import os
import sys
import json
import shutil
import subprocess
import tempfile
import urllib.request
import urllib.error
import urllib.parse

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_meta

# ── API endpoints ──────────────────────────────────────────────────────────
STAC_BASE    = "https://stac.astrogeology.usgs.gov/api"
PDS_API_BASE = "https://pds.nasa.gov/api/search/1"
OPUS_API_BASE = "https://opus.pds-rings.seti.org/opus/api"
DOI_BASE     = "https://doi.org/"

# Curated catalog of USGS planetary COG/GeoTIFF mosaics on
# planetarymaps.usgs.gov. These global products serve byte ranges, so GDAL
# can window-read just the active region via /vsicurl/ instead of downloading
# the whole file. Each entry: key -> (url, body, description). All URLs below
# were verified live (HTTP 200, Accept-Ranges: bytes).
USGS_COG_BASE = "https://planetarymaps.usgs.gov/mosaic"
USGS_COG = {
    "moon_lola_dem_118m": (
        f"{USGS_COG_BASE}/Lunar_LRO_LOLA_Global_LDEM_118m_Mar2014.tif",
        "Moon", "LRO LOLA global DEM, 118 m/px, SimpleCylindrical"),
    "moon_lola_clrshade_128ppd": (
        f"{USGS_COG_BASE}/Lunar_LRO_LOLA_ClrShade_Global_128ppd_v04.tif",
        "Moon", "LRO LOLA global colour-shaded relief, 128 ppd"),
    "mars_mola_dem_463m": (
        f"{USGS_COG_BASE}/Mars_MGS_MOLA_DEM_mosaic_global_463m.tif",
        "Mars", "MGS MOLA global DEM, 463 m/px"),
}

# Prefer these formats (checked in order against the STAC asset media-types
# and file extensions).
PREFERRED_EXT  = (".tif", ".tiff", ".img", ".qub", ".fits", ".xml")
IMPORT_BY_EXT  = {
    ".tif":  "gdal",
    ".tiff": "gdal",
    ".img":  "pds3",     # PDS3 image; p.in.pds3 handles
    ".qub":  "pds3",     # PDS3 cube (VIMS, ISS hyperspectral); p.in.pds3 handles
    ".fits": "gdal",
    ".xml":  "pds4",     # PDS4 label; p.in.pds4 handles
}


# ── Active GRASS region helpers ────────────────────────────────────────────

def read_active_region():
    """
    Return the current GRASS region as a [west, south, east, north] bbox
    suitable for STAC / OGC API spatial filtering, or None when the region
    cannot be determined (e.g. running outside a GRASS session).

    The region is read via g.region -pg (parseable output, geographic
    projection info). For a planetary body with a non-WGS84 datum, the
    coordinate values are still longitude/latitude in degrees (GRASS
    always stores them that way in WIND), so they can be passed directly
    to the STAC bbox filter without reprojection.
    """
    try:
        reg = gs.region()
    except Exception:
        return None
    w = reg.get("w")
    s = reg.get("s")
    e = reg.get("e")
    n = reg.get("n")
    if None in (w, s, e, n):
        return None
    # Clamp to [-180,180] / [-90,90] so STAC bbox is valid
    w = max(-180.0, float(w))
    s = max(-90.0,  float(s))
    e = min( 180.0, float(e))
    n = min(  90.0, float(n))
    if w >= e or s >= n:
        gs.warning("Active region has zero or negative extent; "
                   "spatial pre-filter disabled.")
        return None
    return [w, s, e, n]


def describe_region(bbox):
    """Format a [W,S,E,N] bbox for display."""
    if not bbox:
        return "none (global search)"
    return f"W={bbox[0]:.3f} S={bbox[1]:.3f} E={bbox[2]:.3f} N={bbox[3]:.3f}"


# ── HTTP helpers ───────────────────────────────────────────────────────────

def http_get_json(url, timeout=30):
    """GET *url*, parse JSON, return dict. Raises on HTTP error."""
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        gs.fatal(f"HTTP {e.code} fetching {url}: {e.reason}")
    except urllib.error.URLError as e:
        gs.fatal(f"Network error fetching {url}: {e.reason}")


def http_post_json(url, payload, timeout=30):
    """POST JSON *payload* to *url*, return parsed JSON response."""
    data = json.dumps(payload).encode()
    req  = urllib.request.Request(url, data=data,
                                   headers={"Content-Type": "application/json",
                                            "Accept":       "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        gs.fatal(f"HTTP {e.code} posting to {url}: {e.reason}")
    except urllib.error.URLError as e:
        gs.fatal(f"Network error posting to {url}: {e.reason}")


def resolve_doi(doi_str):
    """Follow doi.org redirect, return the final URL."""
    doi = doi_str.strip()
    if not doi.startswith("http"):
        doi = DOI_BASE + doi.lstrip("/")
    req = urllib.request.Request(doi,
                                  headers={"Accept": "application/json"},
                                  method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.url
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            return e.headers.get("Location", doi)
        gs.warning(f"DOI resolve returned HTTP {e.code}; using bare URL.")
        return doi
    except urllib.error.URLError as e:
        gs.fatal(f"Cannot resolve DOI {doi_str}: {e.reason}")


# ── STAC search (Astropedia) ───────────────────────────────────────────────

def stac_search(keywords=None, ids=None, limit=10, bbox=None):
    """
    Search the USGS Astropedia STAC catalog.
    *keywords*: free-text string.
    *ids*: list of STAC item IDs to retrieve directly.
    *bbox*: [west, south, east, north] in degrees to spatially pre-filter.
    Returns a list of STAC item dicts.
    """
    url  = f"{STAC_BASE}/search"
    body = {"limit": limit}
    if ids:
        body["ids"] = ids
    if keywords:
        body["q"] = keywords
    if bbox:
        body["bbox"] = bbox   # STAC API Extension: Bounding Box
    resp = http_post_json(url, body)
    return resp.get("features", [])


def stac_collection_items(collection_id, limit=10):
    """List items from a specific Astropedia STAC collection."""
    url  = f"{STAC_BASE}/collections/{collection_id}/items?limit={limit}"
    resp = http_get_json(url)
    return resp.get("features", [])


# ── PDS4 Federated Search API ──────────────────────────────────────────────

def _pds_bbox_param(bbox):
    """Format a [W,S,E,N] bbox as a PDS API bbox query parameter."""
    if not bbox:
        return ""
    return f"&bbox={bbox[0]},{bbox[1]},{bbox[2]},{bbox[3]}"


def pds_search_by_lid(lid, limit=1, bbox=None):
    """Query the NASA PDS Search API for a product with the given LID."""
    q   = urllib.parse.quote(f'pds:Logical_Identifier eq "{lid}"')
    url = (f"{PDS_API_BASE}/products"
           f"?q={q}&limit={limit}"
           f"&fields=pds:Logical_Identifier,pds:title,"
           f"ops:Data_File_Info"
           f"{_pds_bbox_param(bbox)}")
    return http_get_json(url).get("data", [])


def pds_search_by_keyword(keyword, limit=10, bbox=None):
    """Free-text search against the NASA PDS Search API."""
    q   = urllib.parse.quote(keyword)
    url = (f"{PDS_API_BASE}/products"
           f"?q={q}&limit={limit}"
           f"&fields=pds:Logical_Identifier,pds:title,"
           f"ops:Data_File_Info"
           f"{_pds_bbox_param(bbox)}")
    return http_get_json(url).get("data", [])


# ── OPUS search (PDS Ring-Moon Systems Node) ───────────────────────────────

def _parse_opus_query(query_str):
    """Parse 'key=value,key=value,...' into a dict of OPUS query params.

    Commas inside values are not supported (use the OPUS API directly in that
    case). Spaces are preserved so that multi-word instrument names work
    (e.g. ``instrument=Cassini VIMS``).
    """
    params = {}
    for part in query_str.split(","):
        part = part.strip()
        if "=" in part:
            k, v = part.split("=", 1)
            params[k.strip()] = v.strip()
    return params


def opus_search(params, limit=10):
    """Query OPUS /api/data.json.

    *params* is a dict of OPUS API search parameters, e.g.
    ``{"instrument": "Cassini VIMS", "target": "Saturn"}``.

    Returns (labels, rows) where *labels* is a list of column names and
    *rows* is a list of dicts keyed by those labels.
    """
    p = dict(params)
    p.setdefault("limit", limit)
    p.setdefault("page", 1)
    p.setdefault("order", "time1,opusid")
    qs  = urllib.parse.urlencode(p)
    url = f"{OPUS_API_BASE}/data.json?{qs}"
    resp   = http_get_json(url)
    labels = resp.get("labels", [])
    rows   = resp.get("page",   [])
    dicts  = [
        dict(zip(labels, row)) if isinstance(row, list) else row
        for row in rows
    ]
    return labels, dicts


def opus_files(opus_id):
    """Return downloadable files for *opus_id* from OPUS /api/files/<id>.json.

    Handles both the nested ``{"data": {id: {...}}}`` form and the flat
    ``{id: {...}}`` form that different OPUS versions may return.

    Returns a list of ``(url, filename, product_type)`` tuples, sorted so
    ``.qub`` cubes come first and ``.lbl`` labels come second.
    """
    # Strip _vis/_ir suffix: the files API keys on the base observation ID.
    base = opus_id
    for suf in ("_vis", "_ir", "_VIS", "_IR"):
        if base.endswith(suf):
            base = base[: -len(suf)]
            break
    url  = f"{OPUS_API_BASE}/files/{base}.json"
    resp = http_get_json(url)

    # Normalise: may be wrapped under "data" key or keyed directly by obs ID.
    data = resp.get("data", resp)
    obs  = data.get(base, data)
    if not isinstance(obs, dict):
        gs.warning(f"OPUS files API returned unexpected format for '{base}'.")
        return []

    files = []
    for ptype, entries in obs.items():
        if not isinstance(entries, list):
            continue
        for entry in entries:
            # API returns either plain URL strings or dicts with a "url" key.
            if isinstance(entry, str):
                href = entry.strip()
            else:
                href = (entry.get("url") or entry.get("href") or "").strip()
            if not href:
                continue
            fname = os.path.basename(urllib.parse.urlparse(href).path)
            files.append((href, fname, ptype))

    def _rank(item):
        ext = os.path.splitext(item[1].lower())[1]
        return {".qub": 0, ".img": 1, ".lbl": 2}.get(ext, 3)
    files.sort(key=_rank)
    return files


def _pick_raw_product(file_list, channel="vis"):
    """Return (url, filename) for the best raw data file in *file_list*.

    Priority:
      1. VIMS channel-specific ``_<channel>.qub`` (e.g. ``_vis.qub``).
      2. Any ``.qub`` file (other VIMS or generic cube).
      3. Any ``.img`` file from a ``*raw*`` product type (ISS raw image).
      4. Any ``.img`` file (ISS or other PDS3 image).
    """
    suffix = f"_{channel.lower()}.qub"
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(suffix):
            return url, fname
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(".qub"):
            return url, fname
    for url, fname, ptype in file_list:
        if fname.lower().endswith(".img") and "raw" in ptype.lower():
            return url, fname
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(".img"):
            return url, fname
    return None, None


def _opus_id_from_row(row, labels):
    """Extract the OPUS ID string from a search result row dict."""
    id_key = next(
        (l for l in labels if "opus" in l.lower() and "id" in l.lower()),
        next((l for l in labels if l.lower() == "opusid"), None),
    )
    if id_key:
        return str(row.get(id_key, ""))
    # fallback: first column
    return str(next(iter(row.values()), ""))


def print_opus_results(labels, rows):
    if not rows:
        gs.message("No OPUS observations found.")
        return
    id_key  = next((l for l in labels
                    if "opus" in l.lower() and "id" in l.lower()), labels[0] if labels else "OPUS ID")
    tgt_key = next((l for l in labels if "target"  in l.lower()), None)
    t1_key  = next((l for l in labels
                    if "start" in l.lower() or "time1" in l.lower()), None)
    gs.message(f"{'OPUS ID':<38} {'Target':<14} Start Time")
    gs.message("-" * 75)
    for row in rows:
        oid = str(row.get(id_key, "?"))[:38]
        tgt = str(row.get(tgt_key, ""))[:14] if tgt_key else ""
        t1  = str(row.get(t1_key,  ""))      if t1_key  else ""
        gs.message(f"{oid:<38} {tgt:<14} {t1}")


# ── Asset selection ────────────────────────────────────────────────────────

def best_asset(stac_item):
    """
    Return (url, filename) for the best downloadable asset in a STAC item.
    Preference order: GeoTIFF > IMG > FITS > anything else.
    """
    assets = stac_item.get("assets", {})
    buckets = {ext: [] for ext in PREFERRED_EXT}
    others  = []
    for key, asset in assets.items():
        href = asset.get("href", "")
        ext  = os.path.splitext(href.lower())[1]
        if ext in buckets:
            buckets[ext].append(href)
        else:
            others.append(href)
    for ext in PREFERRED_EXT:
        if buckets[ext]:
            url = buckets[ext][0]
            return url, os.path.basename(urllib.parse.urlparse(url).path)
    if others:
        url = others[0]
        return url, os.path.basename(urllib.parse.urlparse(url).path)
    return None, None


def pds_product_download_url(pds_product):
    """Extract the first file download URL from a PDS API product dict."""
    props = pds_product.get("properties", {})
    info  = props.get("ops:Data_File_Info", {})
    if isinstance(info, list):
        info = info[0]
    ref = info.get("ops:file_ref") or info.get("ops:file_name")
    if ref:
        return ref, os.path.basename(urllib.parse.urlparse(ref).path)
    return None, None


# ── Download ───────────────────────────────────────────────────────────────

def download_file(url, dest_dir):
    """Download *url* to *dest_dir*, return local path. Shows progress."""
    fname = os.path.basename(urllib.parse.urlparse(url).path) or "product"
    dest  = os.path.join(dest_dir, fname)
    gs.message(f"Downloading {url}")
    try:
        urllib.request.urlretrieve(url, dest)
    except urllib.error.URLError as e:
        gs.fatal(f"Download failed: {e.reason}")
    size_mb = os.path.getsize(dest) / 1048576
    gs.message(f"  -> {dest} ({size_mb:.1f} MB)")
    return dest


# ── Import into GRASS ──────────────────────────────────────────────────────

def import_file(local_path, output_name, band=1, override_proj=False):
    """
    Import *local_path* into GRASS raster *output_name*.
    Dispatches to r.in.gdal (GeoTIFF/FITS), p.in.pds4 (XML), or p.in.pds3 (IMG).
    """
    ext    = os.path.splitext(local_path.lower())[1]
    method = IMPORT_BY_EXT.get(ext, "gdal")
    flags  = "o" if override_proj else ""

    gs.message(f"Importing via {method}: band {band}")

    if method == "gdal":
        gs.run_command("r.in.gdal",
                       flags=flags,
                       input=local_path,
                       output=output_name,
                       band=band,
                       overwrite=True)
    elif method == "pds4":
        gs.run_command("p.in.pds4",
                       flags=flags,
                       input=local_path,
                       output=output_name,
                       overwrite=True)
    elif method == "pds3":
        # p.in.pds3 auto-detects the companion .lbl if present
        gs.run_command("p.in.pds3",
                       flags=flags,
                       input=local_path,
                       output=output_name,
                       overwrite=True)
    else:
        gs.fatal(f"No importer for file extension '{ext}'")


# ── USGS COG mosaics (planetarymaps.usgs.gov) ───────────────────────────────

def resolve_cog(cog_arg):
    """Resolve a cog= argument to a download URL. Accepts a catalog key
    (see USGS_COG) or a direct http(s) URL. Returns (url, body_hint or None)."""
    a = cog_arg.strip()
    if a.lower().startswith(("http://", "https://")):
        return a, None
    if a in USGS_COG:
        url, body, _desc = USGS_COG[a]
        return url, body
    gs.fatal(f"Unknown COG key '{a}'. Use -l to list catalog keys, "
             "or pass a direct https URL.")


# Body-name segments recognised in S3/HTTP URL paths (astrogeo-ard, USGS, PDS).
# Order matters: longer/distinctive names first so substrings don't shadow.
_BODY_PATH_TOKENS = ("mercury", "venus", "earth", "moon", "mars",
                     "jupiter", "saturn", "uranus", "neptune",
                     "ceres", "vesta", "pluto", "charon",
                     "io", "europa", "ganymede", "callisto",
                     "titan", "enceladus", "mimas", "tethys", "dione",
                     "rhea", "iapetus", "phobos", "deimos", "comet")


def _infer_body_from_url(url, body_hint=None):
    """Return a normalised body slug (lowercase) for use in ~/RSDATA/<body>/.

    Priority:
      1. explicit *body_hint* (from catalog).
      2. first matching path-segment token (e.g. ``/mars/``, ``/europa/``).
      3. first matching token in the filename basename, split on the common
         planetary-product separators ``_``, ``-`` and ``.`` (e.g.
         ``Ceres_Dawn_FC_HAMO_DTM_...`` → ``ceres``;
         ``Lunar_LRO_LOLA_Global_LDEM_...`` → matches ``moon``? no — needs
         the body name itself, not the mission. Mission prefixes like LRO,
         MRO, Dawn, Cassini are intentionally NOT mapped: they would
         conflict with the catalog body field for misclassified files).
      4. ``misc`` fallback.
    """
    if body_hint:
        return body_hint.strip().lower()
    try:
        u = urllib.parse.urlparse(url)
    except Exception:
        return "misc"
    path_lower = u.path.lower()
    # Pass 1: full path segments. Most reliable when present
    # (astrogeo-ard, /jupiter/europa/..., follows this convention).
    for seg in path_lower.split("/"):
        if seg in _BODY_PATH_TOKENS:
            return seg
    # Pass 2: basename tokens. USGS planetarymaps mosaics encode the body
    # in the filename (Ceres_Dawn_..., Mars_MGS_..., Lunar_LRO_..., etc.).
    import re
    basename = os.path.basename(path_lower)
    for tok in re.split(r"[_\-.]+", basename):
        if tok in _BODY_PATH_TOKENS:
            return tok
        # Common planetary-product synonyms.
        if tok == "lunar":
            return "moon"
    return "misc"


def _rsdata_dest(url, body_hint=None):
    """Return absolute local path under ~/RSDATA/<Body>/<basename>."""
    body = _infer_body_from_url(url, body_hint)
    # Capitalise the directory: Mars, Moon, Titan… matches the convention
    # already used by the project (e.g. ~/RSDATA/Mars).
    body_dir = body.capitalize()
    root = os.path.join(os.path.expanduser("~"), "RSDATA", body_dir)
    os.makedirs(root, exist_ok=True)
    fname = os.path.basename(urllib.parse.urlparse(url).path) or "product.tif"
    return os.path.join(root, fname)


def _wget_resumable(url, dest):
    """Download *url* to *dest* using wget -c (resumable, robust against
    transient HTTP chunk failures). Returns the local path on success."""
    # If a complete file already exists, skip the network round-trip entirely
    # (HEAD-based size check; wget -c would re-validate but we keep it cheap).
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        try:
            req = urllib.request.Request(url, method="HEAD")
            with urllib.request.urlopen(req, timeout=15) as r:
                remote_len = int(r.headers.get("Content-Length", "0"))
            if remote_len > 0 and os.path.getsize(dest) == remote_len:
                size_mb = remote_len / 1048576
                gs.message(f"Cached: {dest} ({size_mb:.1f} MB) — skipping download.")
                return dest
        except Exception:
            pass  # fall through to wget -c

    gs.message(f"Downloading (wget -c) {url}")
    gs.message(f"  -> {dest}")
    try:
        subprocess.check_call(
            ["wget", "-c", "--tries=5", "--timeout=60",
             "--progress=dot:giga", "-O", dest, url])
    except FileNotFoundError:
        gs.fatal("wget not found; install wget to use the COG pre-download path.")
    except subprocess.CalledProcessError as e:
        gs.fatal(f"wget failed (exit {e.returncode}); partial file kept at {dest} "
                 f"for resume on next run.")
    size_mb = os.path.getsize(dest) / 1048576
    gs.message(f"  downloaded {size_mb:.1f} MB")
    return dest


def _align_region_to_raster(raster_name, save_default=False):
    """Set the computational region to *raster_name*'s extent and resolution
    so that downstream modules (r.what, r.info, p.landing, …) operate on the
    full imported raster by default.

    When *save_default* is True, also persist the region as the project's
    DEFAULT_WIND (``g.region -s``). This is appropriate when we have just
    created a fresh project via ``project=`` — the freshly-built
    DEFAULT_WIND is a generic stub that does not match the raster's extent,
    and saving here makes the project usable out of the box in future
    sessions without the user having to re-run g.region first.
    """
    flags = "s" if save_default else ""
    gs.run_command("g.region", raster=raster_name, flags=flags, quiet=True)
    if save_default:
        gs.message(f"Region set to <{raster_name}> extent and saved as the "
                   f"project's DEFAULT_WIND.")
    else:
        gs.message(f"Region set to <{raster_name}> extent.")


def import_cog(url, output_name, use_region, override_proj,
               body_hint=None, save_default_region=False):
    """Import a COG/GeoTIFF.

    Behaviour:
      * Local path (``url`` doesn't start with http(s))            → r.import
        directly, optionally windowed to the active region.
      * Remote URL                                                  → the file
        is pre-downloaded (``wget -c``, resumable) into
        ``~/RSDATA/<Body>/<basename>``, then r.import is invoked on the local
        copy. This eliminates the transient ``/vsicurl/`` chunk-read failures
        that plagued large HiRISE/PDS COGs from S3. Body is taken from the
        catalog entry when ``cog=<key>`` is used, otherwise inferred from the
        URL path (``…/mars/…`` → ``Mars``, etc.).

    After a successful import, the computational region is aligned to the
    imported raster's extent and resolution so that ad-hoc queries (r.what,
    r.info) and downstream pipelines (p.landing) work without an extra
    g.region step. With *save_default_region=True* (used on the auto-project
    path), the region is also persisted as the project's DEFAULT_WIND.
    """
    if url.lower().startswith(("http://", "https://")):
        local = _rsdata_dest(url, body_hint)
        _wget_resumable(url, local)
        src = local
    else:
        src = url

    flags = ""
    if override_proj:
        flags += "o"
    kw = dict(input=src, output=output_name, overwrite=True)
    if use_region:
        gs.message("Importing local COG, windowed to active region (extent=region)…")
        kw["extent"] = "region"
        kw["resolution"] = "region"
    else:
        gs.message("Importing local COG in full (no region clip; -r given)…")
    if flags:
        kw["flags"] = flags
    gs.run_command("r.import", **kw)

    _align_region_to_raster(output_name, save_default=save_default_region)


def print_cog_catalog():
    gs.message("USGS COG mosaics (use cog=<key>, or a direct https URL):")
    gs.message(f"  {'key':<28} {'body':<6} description")
    gs.message("  " + "-" * 78)
    for k, (url, body, desc) in USGS_COG.items():
        gs.message(f"  {k:<28} {body:<6} {desc}")


# ── Listing ────────────────────────────────────────────────────────────────

def print_stac_items(items):
    if not items:
        gs.message("No matching products found.")
        return
    gs.message(f"{'ID':<40} {'Title':<50} Assets")
    gs.message("-" * 100)
    for it in items:
        iid    = it.get("id", "?")[:40]
        title  = (it.get("properties", {}).get("title", "") or
                  it.get("id", ""))[:50]
        assets = ", ".join(it.get("assets", {}).keys())
        gs.message(f"{iid:<40} {title:<50} {assets}")


def print_pds_products(products):
    if not products:
        gs.message("No matching PDS products found.")
        return
    gs.message(f"{'LID':<60} {'Title':<40}")
    gs.message("-" * 102)
    for p in products:
        props = p.get("properties", {})
        lid   = props.get("pds:Logical_Identifier", ["?"])[0][:60]
        title = props.get("pds:title", ["?"])[0][:40]
        gs.message(f"{lid:<60} {title:<40}")


# ── Main ───────────────────────────────────────────────────────────────────

def _enter_project_for_source(project_name, src_for_crs):
    """Ensure a GRASS project named *project_name* exists at *src_for_crs*'s
    CRS and switch the current session into its PERMANENT mapset.

    *src_for_crs* may be a local file path or a /vsicurl/ URL; the WKT is
    extracted via `gdalsrsinfo -o wkt`. If the project already exists, we
    just switch into it (no CRS check — caller is asserting the project
    is the right one). The caller is responsible for restoring the
    original session at exit; we return (original_location, original_mapset)
    so the caller can do that.
    """
    env = gs.gisenv()
    orig_loc, orig_mapset = env["LOCATION_NAME"], env["MAPSET"]
    gisdb = env["GISDBASE"]
    proj_path = os.path.join(gisdb, project_name)

    if not os.path.isdir(os.path.join(proj_path, "PERMANENT")):
        gs.message(f"Project '{project_name}' does not exist; "
                   f"creating it at the source dataset's CRS…")
        # Extract WKT from the source via gdalsrsinfo. Works for both local
        # files and /vsicurl/ remote COGs (small HTTP read of the header).
        try:
            wkt = subprocess.check_output(
                ["gdalsrsinfo", "-o", "wkt", src_for_crs],
                stderr=subprocess.STDOUT,
            ).decode("utf-8", errors="replace").strip()
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            gs.fatal(f"gdalsrsinfo failed on {src_for_crs}: {e}")
        if not wkt:
            gs.fatal(f"gdalsrsinfo returned empty WKT for {src_for_crs}; "
                     "the source has no CRS metadata — cannot auto-project.")
        with tempfile.NamedTemporaryFile("w", suffix=".wkt", delete=False) as f:
            f.write(wkt)
            wkt_path = f.name
        try:
            gs.run_command("g.proj", project=project_name, wkt=wkt_path,
                           flags="c", quiet=True)
        finally:
            try:
                os.unlink(wkt_path)
            except OSError:
                pass

    # Switch the running session into the (now-existing) project.
    gs.message(f"Switching session into project '{project_name}' / PERMANENT…")
    gs.run_command("g.mapset", project=project_name, mapset="PERMANENT",
                   flags="c", quiet=True)
    return orig_loc, orig_mapset


def _restore_project(orig_loc, orig_mapset):
    """Best-effort switch back to the caller's original project/mapset."""
    try:
        gs.run_command("g.mapset", project=orig_loc, mapset=orig_mapset,
                       quiet=True)
    except Exception:
        pass


def main():
    opt_doi          = options["doi"]
    opt_lid          = options["lid"]
    opt_search       = options["search"]
    opt_cog          = options["cog"]
    opt_opus         = options["opus"]
    opt_opus_id      = options["opus_id"]
    opt_vims_channel = options["vims_channel"] or "vis"
    opt_output       = options["output"]
    opt_band         = int(options["band"])
    opt_limit        = int(options["limit"])
    opt_download_dir = options["download_dir"]
    opt_project      = options["project"]
    flag_list        = flags["l"]
    flag_keep        = flags["k"]
    flag_override    = flags["o"]
    flag_noregion    = flags["r"]

    # ── COG catalog listing / import (independent of the STAC/PDS/OPUS path) ──
    if flag_list and not any((opt_doi, opt_lid, opt_search, opt_opus, opt_opus_id)):
        print_cog_catalog()
        if not opt_cog:
            return

    if opt_cog:
        if any((opt_doi, opt_lid, opt_search)):
            gs.fatal("cog= cannot be combined with doi=/lid=/search=.")
        if flag_list:
            return
        if not opt_output:
            gs.fatal("output= is required to import a COG.")
        url, body_hint = resolve_cog(opt_cog)
        gs.message(f"COG source: {url}")

        # Pre-download remote COGs before any further work (project CRS
        # probe, r.import). This avoids the transient /vsicurl/ chunk-read
        # failures that bite large HiRISE/PDS tiles from S3.
        if url.lower().startswith(("http://", "https://")):
            local_path = _rsdata_dest(url, body_hint)
            _wget_resumable(url, local_path)
            src_for_crs = local_path
        else:
            src_for_crs = url
            local_path = url

        orig_loc = orig_mapset = None
        effective_override = flag_override
        if opt_project:
            # Auto-create / enter a project matching the COG's native CRS,
            # so r.import doesn't have to reproject and the imported raster
            # carries its source coordinates faithfully.
            orig_loc, orig_mapset = _enter_project_for_source(opt_project, src_for_crs)
            # When the project was just built from the source's own WKT
            # (or matches a pre-existing one of the same name), the CRSs
            # are equivalent by construction but the WKT string can still
            # differ in cosmetic detail (e.g. "Mars (2015) - Sphere /
            # Ocentric / Equirectangular" vs "Equirectangular Mars").
            # r.import's strict WKT match would then reject the import,
            # so we force -o on whenever project= is used.
            effective_override = True
        try:
            # local_path is the already-downloaded file (or the original
            # local path if cog= was a local file). Skips the download step
            # inside import_cog because that branch is local-only now.
            import_cog(local_path, opt_output,
                       use_region=not flag_noregion,
                       override_proj=effective_override,
                       body_hint=body_hint,
                       save_default_region=bool(opt_project))
            p_meta.write_planetary_metadata(
                opt_output,
                module="p.in.astropedia",
                command=" ".join(sys.argv),
                data_type="image",
                body=body_hint.upper() if body_hint else None,
                source_file=local_path,
            )
            gs.message(f"Imported COG as GRASS raster '{opt_output}' "
                       f"(project: {opt_project or orig_loc or 'current'}).")
        finally:
            if orig_loc:
                _restore_project(orig_loc, orig_mapset)
        return

    # ── OPUS path: opus_id= or opus= ──────────────────────────────────────
    if opt_opus_id or opt_opus:
        if any((opt_doi, opt_lid, opt_search, opt_cog)):
            gs.fatal("opus= / opus_id= cannot be combined with "
                     "doi=, lid=, search=, or cog=.")
        if opt_opus_id and opt_opus:
            gs.fatal("Provide either opus= (search) or opus_id= (direct), not both.")
        if not flag_list and not opt_output:
            gs.fatal("output= is required unless -l (list only) is given.")

        dest_dir = (opt_download_dir or
                    os.path.join(os.path.expanduser("~"), "RSDATA", "Saturn"))
        os.makedirs(dest_dir, exist_ok=True)

        if opt_opus_id:
            # Direct download: skip search, go straight to files API.
            opus_id = opt_opus_id.strip()
            gs.message(f"Fetching OPUS file list for: {opus_id}")
            file_list = opus_files(opus_id)
            if not file_list:
                gs.fatal(f"No downloadable files found for OPUS ID '{opus_id}'.")
            labels, rows = [], [{"OPUS ID": opus_id}]
        else:
            # Search OPUS with the supplied query params.
            params = _parse_opus_query(opt_opus)
            gs.message(f"Searching OPUS: {params}")
            labels, rows = opus_search(params, limit=opt_limit)
            if flag_list:
                print_opus_results(labels, rows)
                return
            if not rows:
                gs.fatal("No OPUS observations matched the search query.")
            opus_id  = _opus_id_from_row(rows[0], labels)
            gs.message(f"Selected OPUS observation: {opus_id}")
            file_list = opus_files(opus_id)
            if not file_list:
                gs.fatal(f"No downloadable files for OPUS ID '{opus_id}'.")

        # Determine which file to download (.qub for VIMS, .img for ISS).
        dl_url, dl_fname = _pick_raw_product(file_list, opt_vims_channel)
        if not dl_url:
            gs.fatal(
                f"No raw data file (.qub/.img) found for OPUS observation "
                f"'{opus_id}'. Available files:\n" +
                "\n".join(f"  {f} ({pt})" for _, f, pt in file_list[:10])
            )

        # Infer body from URL (usually /COVIMS_…/saturn/…) — default Saturn.
        body_slug = _infer_body_from_url(dl_url) or "saturn"
        dest = os.path.join(
            os.path.expanduser("~"), "RSDATA", body_slug.capitalize(), dl_fname
        )
        os.makedirs(os.path.dirname(dest), exist_ok=True)

        # Also fetch the companion label if present (p.in.pds3 needs it).
        lbl_url = lbl_fname = None
        for url, fname, _pt in file_list:
            if fname.lower().endswith((".lbl", ".LBL")):
                lbl_url = url
                lbl_fname = os.path.join(os.path.dirname(dest), fname)
                break
        if lbl_url:
            gs.message(f"Fetching label: {lbl_url}")
            _wget_resumable(lbl_url, lbl_fname)

        gs.message(f"Fetching cube ({opt_vims_channel.upper()}): {dl_url}")
        _wget_resumable(dl_url, dest)

        # Import via p.in.pds3 (handles both .img PDS3 images and .qub cubes).
        ext = os.path.splitext(dl_fname.lower())[1]
        if ext == ".qub":
            kind = f"VIMS {opt_vims_channel.upper()} cube"
        else:
            kind = "PDS3 image"
        gs.message(f"Importing {kind} via p.in.pds3 …")
        gs.run_command("p.in.pds3",
                       flags="o" if flag_override else "",
                       input=dest, output=opt_output, overwrite=True)
        _align_region_to_raster(opt_output, save_default=False)

        # Infer sensor from OPUS ID prefix (co-iss-n* / co-iss-w* / co-vims-*).
        oid_lower = opus_id.lower()
        if oid_lower.startswith("co-iss-n"):
            _sensor = "CASSINI_ISS_NAC"
        elif oid_lower.startswith("co-iss-w"):
            _sensor = "CASSINI_ISS_WAC"
        elif oid_lower.startswith("co-vims"):
            _sensor = "CASSINI_VIMS"
        else:
            _sensor = None

        # Infer target body from first search result, if available.
        _body_val = body_slug.upper() if body_slug != "misc" else None
        if rows:
            tgt_key = next((l for l in labels if "target" in l.lower()), None)
            if tgt_key:
                _body_val = str(rows[0].get(tgt_key, _body_val or "")).upper() or _body_val

        p_meta.write_planetary_metadata(
            opt_output,
            module="p.in.astropedia",
            command=" ".join(sys.argv),
            data_type="image",
            sensor=_sensor,
            mission="CASSINI",
            body=_body_val,
            pds_product_id=opus_id,
            source_file=dl_url,
        )
        gs.message(f"Imported {kind} as '{opt_output}'.")
        return

    # Validate: exactly one of doi/lid/search
    n_src = sum(1 for x in (opt_doi, opt_lid, opt_search) if x)
    if n_src == 0:
        gs.fatal("Provide exactly one of doi=, lid=, search=, cog=, "
                 "opus=, or opus_id= (or -l to list options).")
    if n_src > 1:
        gs.fatal("Provide exactly one of doi=, lid=, or search= "
                 "(got multiple).")
    if not flag_list and not opt_output:
        gs.fatal("output= is required unless -l (list only) is given.")

    dest_dir = opt_download_dir if opt_download_dir else tempfile.mkdtemp(prefix="p_in_astropedia_")
    os.makedirs(dest_dir, exist_ok=True)

    # ── Read active GRASS region for spatial pre-filtering ─────────────
    bbox = None if flag_noregion else read_active_region()
    gs.message(f"Active region bbox: {describe_region(bbox)}")
    if bbox:
        gs.message("  Products whose footprint does not intersect this "
                   "region will be filtered out by the API.")

    # ── Resolve source → STAC items or PDS products ────────────────────
    stac_items   = []
    pds_products = []

    if opt_doi:
        gs.message(f"Resolving DOI: {opt_doi}")
        landing = resolve_doi(opt_doi)
        gs.message(f"  Resolved to: {landing}")
        doi_bare = opt_doi.strip().lstrip("https://doi.org/")
        stac_items = stac_search(keywords=doi_bare, limit=opt_limit,
                                  bbox=bbox)
        if not stac_items:
            stac_items = stac_search(keywords=doi_bare.split("/")[-1],
                                     limit=opt_limit, bbox=bbox)
        if not stac_items:
            gs.warning("DOI did not match any Astropedia STAC items; "
                       "trying NASA PDS API.")
            pds_products = pds_search_by_keyword(doi_bare, limit=opt_limit,
                                                  bbox=bbox)

    elif opt_lid:
        gs.message(f"Searching PDS4 LID: {opt_lid}")
        pds_products = pds_search_by_lid(opt_lid, limit=opt_limit, bbox=bbox)
        if not pds_products:
            gs.message("LID not found in PDS API; trying Astropedia STAC.")
            stac_items = stac_search(keywords=opt_lid.split(":")[-1],
                                     limit=opt_limit, bbox=bbox)

    elif opt_search:
        gs.message(f"Searching Astropedia STAC for: '{opt_search}'")
        stac_items = stac_search(keywords=opt_search, limit=opt_limit,
                                  bbox=bbox)
        if not stac_items:
            gs.message("No STAC results; trying NASA PDS API.")
            pds_products = pds_search_by_keyword(opt_search, limit=opt_limit,
                                                  bbox=bbox)

    # ── List mode ──────────────────────────────────────────────────────
    if flag_list:
        if stac_items:
            gs.message(f"\n=== Astropedia STAC results ({len(stac_items)}) ===")
            print_stac_items(stac_items)
        if pds_products:
            gs.message(f"\n=== NASA PDS results ({len(pds_products)}) ===")
            print_pds_products(pds_products)
        if not stac_items and not pds_products:
            gs.message("No products found.")
        return

    # ── Download + import ──────────────────────────────────────────────
    download_url = None
    file_name    = None

    if stac_items:
        item = stac_items[0]
        gs.message(f"Selected STAC item: {item.get('id')}")
        download_url, file_name = best_asset(item)
    elif pds_products:
        prod = pds_products[0]
        props = prod.get("properties", {})
        lid_v = props.get("pds:Logical_Identifier", ["?"])[0]
        gs.message(f"Selected PDS product: {lid_v}")
        download_url, file_name = pds_product_download_url(prod)

    if not download_url:
        gs.fatal("Could not determine a download URL from the search results. "
                 "Use -l to inspect available assets.")

    # resolve PDS product ID and body from whichever search path was used
    _pds_id = None
    _body   = None
    if stac_items:
        item = stac_items[0]
        _pds_id = item.get("id")
        _body   = (item.get("properties") or {}).get("ssys:targets", [None])[0]
    elif pds_products:
        prod = pds_products[0]
        props = prod.get("properties", {})
        _pds_id = (props.get("pds:Logical_Identifier") or [None])[0]
        _body   = (props.get("ssys:targets") or [None])[0]

    local_path = download_file(download_url, dest_dir)
    try:
        import_file(local_path, opt_output, band=opt_band,
                    override_proj=flag_override)
        # Align region to the imported raster so the project is usable out of
        # the box (mirrors the cog= path). DEFAULT_WIND is intentionally NOT
        # saved here because the DOI/LID/STAC branches don't auto-create a
        # project; the user is importing into an existing one and we
        # shouldn't clobber their stored region.
        _align_region_to_raster(opt_output, save_default=False)
        p_meta.write_planetary_metadata(
            opt_output,
            module="p.in.astropedia",
            command=" ".join(sys.argv),
            data_type="image",
            body=str(_body).upper() if _body else None,
            pds_product_id=_pds_id,
            source_file=download_url,
        )
        gs.message(f"Imported as GRASS raster '{opt_output}'.")
    finally:
        if not flag_keep and not opt_download_dir:
            # Only auto-delete if we created the temp dir ourselves
            try:
                shutil.rmtree(dest_dir)
            except OSError:
                pass


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
