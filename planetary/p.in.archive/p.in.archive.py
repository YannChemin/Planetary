#!/usr/bin/env python3
"""
MODULE:    p.in.archive
AUTHOR:    Yann Chemin <dr.yann.chemin@gmail.com>
PURPOSE:   Fetch and import planetary data products from real remote
           archives: the USGS Astropedia STAC catalog, the NASA PDS
           Federated Search API, the PDS Geosciences Node (CRISM, M3),
           the ESA Planetary Science Archive (OMEGA), or the PDS
           Ring-Moon Systems Node OPUS search interface (Cassini
           ISS/VIMS). Supports DOI resolution, PDS4 LID lookup, keyword
           search, curated USGS COG mosaics, curated CRISM/OMEGA/M3/VIMS
           catalogs, and direct OPUS observation queries. Downloaded
           files are imported via r.in.gdal, p.in.pds4, or p.in.pds3
           depending on file type.
LICENSE:   The Unlicense (https://unlicense.org)
           This is free and unencumbered software released into the public domain.
"""

# %module
# % description: Fetch and import planetary data from real remote archives: USGS Astropedia, NASA PDS, PDS Geosciences Node (CRISM, M3), ESA PSA (OMEGA), or OPUS (Ring-Moon Systems Node, Cassini ISS/VIMS).
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: raster
# % keyword: PDS4
# % keyword: Astropedia
# % keyword: CRISM
# % keyword: OMEGA
# % keyword: M3
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
# % key: crism
# % type: string
# % required: no
# % multiple: no
# % label: MRO/CRISM TRDR product: a catalog key (see -l) or a direct https URL
# % description: Fetches a CRISM Targeted RDR product (.IMG + companion .LBL) from the PDS Geosciences Node static archive (pds-geosciences.wustl.edu). CRISM is not searchable via OPUS (outer-planet/ring-science only) or the NASA PDS Federated Search (TRDR products are not indexed there); this option talks to the real, working archive tree directly. Use -l to list catalog keys.
# %end

# %option
# % key: m3
# % type: string
# % required: no
# % multiple: no
# % label: Chandrayaan-1 M3 L1B product: a catalog key (see -l) or a direct https URL
# % description: Fetches a Moon Mineralogy Mapper (M3) L1B radiance product (*_RDN.IMG + companion *_L1B.LBL) from the JPL PDS Imaging Node static archive (planetarydata.jpl.nasa.gov). Use -l to list catalog keys.
# %end

# %option
# % key: vims
# % type: string
# % required: no
# % multiple: no
# % label: Cassini VIMS observation: a catalog key (see -l) or a direct OPUS id
# % description: Fetches a Cassini VIMS hyperspectral cube via the OPUS API -- a dedicated shortcut into the same opus_id= path below, without needing to hand-build an opus= search query. Use -l to list catalog keys.
# %end

# %option
# % key: omega
# % type: string
# % required: no
# % multiple: no
# % label: Mars Express OMEGA EDR product: a catalog key (see -l) or a direct https URL
# % description: Fetches a Mars Express OMEGA hyperspectral cube (attached-label PDS3 QUBE, single .QUB file -- no separate .LBL) from the ESA Planetary Science Archive (archives.esac.esa.int). Use -l to list catalog keys.
# %end

# %option
# % key: opus_id
# % type: string
# % required: no
# % multiple: no
# % label: OPUS observation ID to download directly
# % description: Download and import a specific OPUS observation, e.g. co-vims-v1799424623. Skips the search step.
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

# %option
# % key: product
# % type: string
# % required: no
# % options: auto,raw,calib
# % answer: auto
# % label: Product calibration level to prefer
# % description: auto: prefer calibrated (CALIB) for ISS, raw otherwise; raw: always import raw PDS3 image (.img without _CALIB); calib: always prefer calibrated product (_CALIB.img).
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

# %flag
# % key: s
# % description: Fetch and attach real SPICE kernels for camera-mode geometry after import (crism= only). Calls p.spice.find then p.spiceinit using the real observation time/body already known from the label. Opt-in: kernel downloads are large (often 100s of MB) and not needed unless you intend to run p.phocube -c on the result.
# %end

# %flag
# % key: g
# % description: Also fetch and import the M3 L1B geometry companion cubes (m3= only): LOC_IMAGE (per-pixel longitude/latitude/radius, 3 bands) and OBS_IMAGE (per-pixel illumination/viewing geometry -- to-sun/to-instrument azimuth and zenith, phase angle, path lengths, facet slope/aspect/cos-i, 10 bands). Unlike CRISM, M3's L1B product ships this geometry precomputed -- no SPICE/camera-model step needed, just importing the extra cubes that are already in the same archive directory as the radiance cube.
# %end

# %option
# % key: project
# % type: string
# % required: no
# % label: Name of GRASS project to create (or switch into) at the dataset's native CRS
# % description: When set, p.in.archive probes the source dataset's CRS (via gdalsrsinfo for /vsicurl/ URLs, or from the local PDS4 label) and either reuses an existing project of that name OR creates one with the matching projection via g.proj. The current GRASS session is then switched into the new project before the raster is imported, eliminating the CRS-mismatch / reproject step that otherwise bites users importing PDS-native products (e.g. Mars MOLA in Equirectangular Mars) into a non-matching working project. The original project is restored at exit.
# %end

import os
import re
import sys
import glob
import json
import shutil
import subprocess
import tempfile
import urllib.request
import urllib.error
import urllib.parse

import grass.script as gs
import grass.exceptions

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_meta
import p_spice

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

# Curated catalog of MRO/CRISM Targeted RDR (TRR3) hyperspectral products on
# the PDS Geosciences Node static archive. CRISM is absent from both OPUS
# (outer-planet/ring-science instruments only) and the NASA PDS Federated
# Search (TRDR products are not indexed there even by exact LID) — the only
# proven-working retrieval path is this archive's volume/year/DOY tree,
# discovered via the per-volume collection inventory CSV. Each entry:
# key -> (img_url, lbl_url, body, description). All URLs below were verified
# live (HTTP 200, Content-Length present) against pds-geosciences.wustl.edu.
CRISM_BASE = ("https://pds-geosciences.wustl.edu/mro/"
              "mro-m-crism-3-rdr-targeted-v1/mrocr_2101/trdr/2007/2007_005/"
              "FRT00003BFB")
CRISM_CATALOG = {
    "mawrth_vallis_frt00003bfb_ir": (
        f"{CRISM_BASE}/FRT00003BFB_01_IF156L_TRR3.IMG",
        f"{CRISM_BASE}/FRT00003BFB_01_IF156L_TRR3.LBL",
        "Mars",
        "CRISM FRT00003BFB, Mawrth Vallis (22.3N, 342.1E), L detector "
        "(IR, 1.00-3.92 um, 438 bands) - Bishop et al. 2008"),
    "mawrth_vallis_frt00003bfb_vnir": (
        f"{CRISM_BASE}/FRT00003BFB_01_IF156S_TRR3.IMG",
        f"{CRISM_BASE}/FRT00003BFB_01_IF156S_TRR3.LBL",
        "Mars",
        "CRISM FRT00003BFB, Mawrth Vallis (22.3N, 342.1E), S detector "
        "(VNIR, 0.36-1.05 um, 107 bands) - Bishop et al. 2008"),
}

# Curated catalog of Chandrayaan-1 Moon Mineralogy Mapper (M3) L1B radiance
# products on the JPL PDS Imaging Node static archive. Detached-label PDS3
# (^RDN_IMAGE pointer nested inside an OBJECT = RDN_FILE wrapper -- not the
# standard IMAGE/QUBE/SPECTRAL_QUBE object name; libs/p_pds gained a generic
# *_IMAGE/*_QUBE object-name fallback to read these). Each entry:
# key -> (img_url, lbl_url, body, description). Verified live (HTTP 200,
# Content-Length present, real import: 85 bands, non-degenerate radiance)
# against planetarydata.jpl.nasa.gov.
M3_BASE = ("https://planetarydata.jpl.nasa.gov/img/data/m3/CH1M3_0003/DATA/"
           "20081118_20090214/200811/L1B")
M3_CATALOG = {
    "m3g20081118t222604_v03_rdn": (
        f"{M3_BASE}/M3G20081118T222604_V03_RDN.IMG",
        f"{M3_BASE}/M3G20081118T222604_V03_L1B.LBL",
        "Moon",
        "M3 L1B radiance, orbit 141, 2008-11-18T22:26:04, GLOBAL mode "
        "(85 bands, 304 samples x 1182 lines)"),
}

# Curated catalog of Cassini VIMS observation IDs (OPUS opus_id=), exposed
# as a dedicated vims= shortcut so VIMS doesn't require hand-building an
# opus= search query. Each entry: key -> (opus_id, body, description).
# Resolved through the existing opus_files()/_pick_raw_product() machinery
# -- no new archive-access code, just a curated entry point into it. IDs
# verified live: real .qub+.lbl files confirmed via the OPUS files API
# (opus.pds-rings.seti.org/opus/api/files/<id>_ir.json).
VIMS_CATALOG = {
    "titan_v1799424623": (
        "co-vims-v1799424623",
        "Titan",
        "Cassini VIMS, Titan flyby, 2015-01-08T15:09:40, 118.5s exposure "
        "(VIS: 0.35-1.05 um/96 bands, IR: 0.88-5.1 um/256 bands)"),
}

# Curated catalog of Mars Express OMEGA EDR products on the ESA Planetary
# Science Archive (archives.esac.esa.int). Attached-label PDS3 QUBE -- a
# single .QUB file carries both label and data, no companion .LBL. Verified
# live: real HTTP 200, real import (352 bands, sane raw DN within the
# label's own declared saturation bounds).
OMEGA_BASE = ("https://archives.esac.esa.int/psa/ftp/MARS-EXPRESS/OMEGA/"
              "MEX-M-OMEGA-2-EDR-FLIGHT-V1.0/DATA")
OMEGA_CATALOG = {
    "orb0100_0": (
        f"{OMEGA_BASE}/ORB01/ORB0100_0.QUB",
        "Mars",
        "OMEGA EDR, orbit 100, 2004-02-10T18:07:10 "
        "(352 bands, 64 samples x 424 lines)"),
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


def _opus_files_one(obs_id):
    """Query OPUS /api/files/<obs_id>.json for exactly *obs_id* (no suffix
    handling). Returns a list of (url, filename, product_type) tuples, or
    [] if the API has no files for this exact id."""
    url  = f"{OPUS_API_BASE}/files/{obs_id}.json"
    resp = http_get_json(url)

    # Normalise: may be wrapped under "data" key or keyed directly by obs ID.
    data = resp.get("data", resp)
    obs  = data.get(obs_id, data)
    if not isinstance(obs, dict):
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
    return files


def opus_files(opus_id, channel=None):
    """Return downloadable files for *opus_id* from OPUS /api/files/<id>.json.

    VIMS observation IDs come back from /api/data.json with a ``_vis``/
    ``_ir`` channel suffix (e.g. ``co-vims-v1799424623_ir``), and the real
    OPUS files API keys on that *exact* suffixed id -- querying the base
    (unsuffixed) id returns an empty result (confirmed live against
    opus.pds-rings.seti.org: ``files/co-vims-v1799424623.json`` -> ``{}``,
    ``files/co-vims-v1799424623_ir.json`` -> real .qub/.lbl URLs).

    Lookup order:
      1. *opus_id* exactly as given (correct as-is for search results,
         which already come back suffixed).
      2. If that's empty and *opus_id* has no ``_vis``/``_ir`` suffix yet
         and *channel* was given (the ``vims_channel=`` option, for a
         user-supplied bare ``opus_id=``): retry with that suffix appended.
      3. If *opus_id* does carry a suffix and step 1 still came back empty:
         retry with the suffix stripped, in case some other archive
         convention genuinely needs the base id.

    Returns a list of ``(url, filename, product_type)`` tuples, sorted so
    ``.qub`` cubes come first and ``.lbl`` labels come second.
    """
    has_suffix = opus_id.lower().endswith(("_vis", "_ir"))
    files = _opus_files_one(opus_id)
    if not files and not has_suffix and channel:
        files = _opus_files_one(f"{opus_id}_{channel.lower()}")
    if not files and has_suffix:
        base = opus_id
        for suf in ("_vis", "_ir", "_VIS", "_IR"):
            if base.endswith(suf):
                base = base[: -len(suf)]
                break
        files = _opus_files_one(base)
    if not files:
        gs.warning(f"OPUS files API returned no files for '{opus_id}'.")
        return []

    def _rank(item):
        ext = os.path.splitext(item[1].lower())[1]
        return {".qub": 0, ".img": 1, ".lbl": 2}.get(ext, 3)
    files.sort(key=_rank)
    return files


def _pick_raw_product(file_list, channel="vis", product="auto"):
    """Return (url, filename) for the best data file in *file_list*.

    ``product`` controls calibration preference for ISS .img files:
      - ``auto``  (default): prefer _CALIB.img, fall back to raw .img.
      - ``calib``: require _CALIB.img; fall back to raw .img only if absent.
      - ``raw``:   skip all _CALIB files; take the raw .img directly.

    Full priority order (VIMS takes precedence regardless of ``product``):
      1. VIMS channel-specific ``_<channel>.qub`` (e.g. ``_vis.qub``).
      2. Any ``.qub`` file.
      3. Calibrated ISS ``_CALIB.img``  (skipped when product=raw).
      4. Any ``.img`` from a ``*raw*`` product type.
      5. Any ``.img``.
    """
    suffix = f"_{channel.lower()}.qub"
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(suffix):
            return url, fname
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(".qub"):
            return url, fname
    if product in ("auto", "calib"):
        for url, fname, _ptype in file_list:
            if fname.lower().endswith(".img") and "_calib" in fname.lower():
                return url, fname
    for url, fname, ptype in file_list:
        if fname.lower().endswith(".img") and "raw" in ptype.lower():
            return url, fname
    for url, fname, _ptype in file_list:
        if fname.lower().endswith(".img"):
            return url, fname
    return None, None


def _pds3_image_shape(lbl_path):
    """Parse LINES and LINE_SAMPLES from a PDS3 label file."""
    lines = samples = None
    try:
        with open(lbl_path, "r", errors="ignore") as fh:
            for line in fh:
                kv = line.split("=", 1)
                if len(kv) == 2:
                    key = kv[0].strip()
                    val = kv[1].strip().split()[0].rstrip(";")
                    if key == "LINES" and lines is None:
                        try: lines = int(val)
                        except ValueError: pass
                    elif key == "LINE_SAMPLES" and samples is None:
                        try: samples = int(val)
                        except ValueError: pass
                if lines and samples:
                    break
    except OSError:
        pass
    return lines, samples


def _pds3_label_field(lbl_path, key):
    """Parse a single 'KEY = value' field from a PDS3 label file.

    Generalizes _pds3_image_shape()'s line-scanner for an arbitrary
    keyword (e.g. START_TIME, MRO:FRAME_RATE). Returns the raw value
    string (quotes/units stripped) or None if not found."""
    try:
        with open(lbl_path, "r", errors="ignore") as fh:
            for line in fh:
                kv = line.split("=", 1)
                if len(kv) != 2:
                    continue
                if kv[0].strip() != key:
                    continue
                val = kv[1].strip()
                # Strip a trailing unit, e.g. "3.75 <HZ>".
                val = val.split("<")[0].strip()
                val = val.strip('"').rstrip(";").strip()
                return val
    except OSError:
        pass
    return None


_RE_FILTER_NAME = re.compile(r'FILTER_NAME\s*=\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')


def _pds3_filter_pair(lbl_path):
    """Parse a Cassini ISS-style 'FILTER_NAME = ("CL1","CL2")' 2-tuple from
    a PDS3 label. Returns "F1/F2" (matching PlanetaryMetadata.filter_name's
    documented convention) or None. p.phocube's -c camera model needs both
    filter names to look up the right INS-<id>_<F1>_<F2>_FOCAL_LENGTH key
    in the real IAK (no single focal length is correct for ISS -- it
    varies per filter combination)."""
    try:
        with open(lbl_path, "r", errors="ignore") as fh:
            text = fh.read()
    except OSError:
        return None
    m = _RE_FILTER_NAME.search(text)
    return f"{m.group(1)}/{m.group(2)}" if m else None


_RE_SAMPLING_MODE_ID = re.compile(
    r'SAMPLING_MODE_ID\s*=\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')
_RE_VIMS_INT_FIELD = {
    "x_offset":    re.compile(r'X_OFFSET\s*=\s*(-?\d+)'),
    "z_offset":    re.compile(r'Z_OFFSET\s*=\s*(-?\d+)'),
    "swath_width": re.compile(r'SWATH_WIDTH\s*=\s*(-?\d+)'),
    "swath_length": re.compile(r'SWATH_LENGTH\s*=\s*(-?\d+)'),
}


def _pds3_vims_geometry(lbl_path):
    """Parse VIMS's real per-cube camera-model geometry fields from a PDS3
    label: SAMPLING_MODE_ID = ("<IR mode>","<visible mode>") plus the
    shared X_OFFSET/Z_OFFSET/SWATH_WIDTH/SWATH_LENGTH (confirmed against
    ISIS3's own vims2isis/main.cpp::TranslateVimsLabels(), which assigns
    SAMPLING_MODE_ID[0] to IR and [1] to VIS). p.phocube -c
    instrument=VIMS_IR/VIMS_VIS needs these -- they are real per-cube
    Instrument-group label values, not SPICE kernel data (see TODO.md).
    Returns a dict (missing fields omitted) or {} if the label can't be
    read."""
    try:
        with open(lbl_path, "r", errors="ignore") as fh:
            text = fh.read()
    except OSError:
        return {}
    out = {}
    m = _RE_SAMPLING_MODE_ID.search(text)
    if m:
        out["sampling_mode_ir"] = m.group(1)
        out["sampling_mode_vis"] = m.group(2)
    for field, pattern in _RE_VIMS_INT_FIELD.items():
        m = pattern.search(text)
        if m:
            out[field] = int(m.group(1))
    return out


def _attach_crism_spice(local_lbl, map_band1, body_slug):
    """Discover and attach real NAIF SPICE kernels for a just-imported CRISM
    TRDR cube, so p.phocube -c can run without further manual setup.

    Reads the real START_TIME/MRO:FRAME_RATE from the label, runs
    p.spice.find (spacecraft=MRO, instrument=CRISM -- fetches CRISM's
    gimbal CK and virtual SCLK in addition to the regular MRO kernels),
    then p.spiceinit with whatever kernels were actually found. Warns
    (does not fail) when some kernel types aren't available for this
    date -- p.spice.find's own SPK/CK matcher has known real-archive gaps
    for some dates (a separate, pre-existing limitation), and a partial
    SPICE context is still strictly better than none."""
    start_time = _pds3_label_field(local_lbl, "START_TIME")
    if not start_time:
        gs.warning("-s: could not read START_TIME from the label; "
                   "skipping SPICE kernel attachment.")
        return
    frame_rate = _pds3_label_field(local_lbl, "MRO:FRAME_RATE")
    line_rate = None
    if frame_rate:
        try:
            line_rate = 1.0 / float(frame_rate)
        except (ValueError, ZeroDivisionError):
            pass

    # str2et()/p.spiceinit want plain ISO time; the real label keeps full
    # sub-second precision (e.g. "2007-01-05T01:26:56.855"), which is fine.
    gs.message(f"-s: discovering SPICE kernels for MRO/CRISM at {start_time} …")
    try:
        gs.run_command("p.spice.find", spacecraft="MRO", instrument="CRISM",
                       time=start_time,
                       kernels="lsk,sclk,ik,fk,pck,spk,ck", quiet=True)
    except grass.exceptions.CalledModuleError as e:
        gs.warning(f"-s: p.spice.find reported an error ({e}); attaching "
                   "whatever kernels were already cached.")

    kdir = p_spice.mapset_spice_dir()
    kernel_globs = {
        "lsk":  ("*.tls",),
        "sclk": ("*.tsc",),
        "ik":   ("*.ti",),
        "fk":   ("*.tf",),
        "pck":  ("*.tpc",),
        "spk":  ("*.bsp",),
        "ck":   ("*.bc",),
    }
    found = {}
    for ktype, patterns in kernel_globs.items():
        paths = []
        for pat in patterns:
            paths.extend(sorted(glob.glob(os.path.join(kdir, ktype, pat))))
        found[ktype] = paths

    if not found["spk"] or not found["ck"]:
        gs.warning("-s: no SPK and/or CK found for this date -- p.phocube -c "
                   "will not be able to compute real intercepts until these "
                   "are available. Attaching what was found regardless.")

    kwargs = dict(map=map_band1, target=body_slug.upper(), observer="MRO",
                  time=start_time, overwrite=True)
    if line_rate:
        kwargs["line_rate"] = line_rate
    for ktype in ("lsk", "sclk", "ik", "fk", "pck", "spk", "ck"):
        if found[ktype]:
            kwargs[ktype] = found[ktype]

    gs.message("-s: attaching SPICE kernels via p.spiceinit …")
    gs.run_command("p.spiceinit", **kwargs)
    gs.message(f"-s: SPICE context attached to '{map_band1}' "
               f"({sum(len(v) for v in found.values())} kernel file(s)).")


def _attach_m3_geometry(local_lbl, img_url, output, body_slug):
    """Fetch and import M3 L1B's LOC/OBS geometry companion cubes.

    Unlike CRISM, M3's L1B product ships per-pixel geometry precomputed
    (no SPICE/camera-model step needed): the same attached label that
    describes RDN_IMAGE also describes LOC_IMAGE (longitude/latitude/
    radius, 3 bands) and OBS_IMAGE (illumination/viewing angles, 10
    bands) -- both pointing at companion *_LOC.IMG/*_OBS.IMG files that
    live alongside the radiance cube in the same archive directory.
    p.in.pds3's object= option (added for this) selects each one out of
    the shared label in turn.
    """
    for suffix in ("_LOC.IMG", "_OBS.IMG"):
        url = re.sub(r"_RDN\.IMG$", suffix, img_url, flags=re.IGNORECASE)
        if url == img_url:
            gs.warning(f"-g: could not derive {suffix} URL from '{img_url}'; "
                       "skipping M3 geometry import.")
            return
        local_path = os.path.join(os.path.dirname(local_lbl),
                                  os.path.basename(url))
        gs.message(f"-g: fetching M3 geometry companion: {url}")
        _wget_resumable(url, local_path)

    band_names = {
        "loc": ("Longitude", "Latitude", "Radius"),
        "obs": ("To-Sun azimuth", "To-Sun zenith", "To-Instrument azimuth",
                "To-Instrument zenith", "Phase angle", "To-Sun path length",
                "To-Instrument path length", "Facet slope", "Facet aspect",
                "Facet cos(i)"),
    }
    for kind, obj_name in (("loc", "LOC_IMAGE"), ("obs", "OBS_IMAGE")):
        out = f"{output}_{kind}"
        gs.message(f"-g: importing M3 {obj_name} via p.in.pds3 …")
        gs.run_command("p.in.pds3", flags="g", input=local_lbl,
                       object=obj_name, output=out, overwrite=True)
        for i, bname in enumerate(band_names[kind], start=1):
            mapname = f"{out}.{i}"
            if p_meta.PlanetaryMetadata.exists(mapname):
                meta = p_meta.PlanetaryMetadata.load(mapname)
                meta.sensor = "CH1_M3"
                meta.mission = "CHANDRAYAAN-1"
                meta.body = body_slug.upper()
                meta.data_type = "ancillary"
                meta.radiometric_quantity = bname
                meta.derived = True
                meta.save(mapname)
        gs.message(f"-g: imported M3 {obj_name} as '{out}.1'..'{out}.{len(band_names[kind])}' "
                   f"({', '.join(band_names[kind])}).")


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
        try:
            gs.run_command("r.in.gdal",
                           flags=flags,
                           input=local_path,
                           output=output_name,
                           band=band,
                           overwrite=True)
        except grass.exceptions.CalledModuleError:
            if override_proj:
                raise
            # USGS Astropedia STAC products (HiRISE, CTX, THEMIS, ...) each
            # carry their own native Mars CRS variant (different central
            # longitude / sphere realisation per mission), which essentially
            # never matches whatever project the user happens to be working
            # in. r.in.gdal requires an exact CRS match; r.import reprojects
            # on the fly into the current project's CRS instead, so retry
            # with it rather than forcing the user to pre-build a
            # CRS-matching project (or pass -o, which would silently treat
            # mismatched coordinates as if they were already in the current
            # CRS — wrong by definition for a genuine CRS mismatch).
            gs.warning("r.in.gdal CRS check failed; retrying with r.import "
                       "(on-the-fly reprojection to the active project's CRS).")
            gs.run_command("r.import",
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


def resolve_crism(crism_arg):
    """Resolve a crism= argument to (img_url, lbl_url, body_hint).

    Accepts a catalog key (see CRISM_CATALOG) or a direct https URL to a
    CRISM TRDR .IMG on pds-geosciences.wustl.edu; the companion .LBL is
    derived by replacing the extension."""
    a = crism_arg.strip()
    if a in CRISM_CATALOG:
        img_url, lbl_url, body, _desc = CRISM_CATALOG[a]
        return img_url, lbl_url, body
    if a.lower().startswith(("http://", "https://")):
        if not a.lower().endswith(".img"):
            gs.fatal("Direct crism= URLs must point at a CRISM TRDR .IMG file.")
        lbl_url = a[: -len(".img")] + (".LBL" if a.endswith(".IMG") else ".lbl")
        return a, lbl_url, None
    gs.fatal(f"Unknown CRISM key '{a}'. Use -l to list catalog keys, "
             "or pass a direct https URL to a TRDR .IMG file.")


def print_crism_catalog():
    gs.message("MRO/CRISM TRDR products (use crism=<key>, or a direct https URL "
               "to a .IMG on pds-geosciences.wustl.edu):")
    gs.message(f"  {'key':<32} {'body':<6} description")
    gs.message("  " + "-" * 90)
    for k, (_img, _lbl, body, desc) in CRISM_CATALOG.items():
        gs.message(f"  {k:<32} {body:<6} {desc}")


def resolve_m3(m3_arg):
    """Resolve an m3= argument to (img_url, lbl_url, body_hint).

    Accepts a catalog key (see M3_CATALOG) or a direct https URL to an
    M3 L1B *_RDN.IMG on planetarydata.jpl.nasa.gov; the companion *_L1B.LBL
    is derived by replacing the _RDN.IMG suffix."""
    a = m3_arg.strip()
    if a in M3_CATALOG:
        img_url, lbl_url, body, _desc = M3_CATALOG[a]
        return img_url, lbl_url, body
    if a.lower().startswith(("http://", "https://")):
        if not a.upper().endswith("_RDN.IMG"):
            gs.fatal("Direct m3= URLs must point at an M3 L1B *_RDN.IMG file.")
        lbl_url = a[: -len("_RDN.IMG")] + "_L1B.LBL"
        return a, lbl_url, None
    gs.fatal(f"Unknown M3 key '{a}'. Use -l to list catalog keys, "
             "or pass a direct https URL to an L1B *_RDN.IMG file.")


def print_m3_catalog():
    gs.message("Chandrayaan-1 M3 L1B radiance products (use m3=<key>, or a "
               "direct https URL to a *_RDN.IMG on planetarydata.jpl.nasa.gov):")
    gs.message(f"  {'key':<28} {'body':<6} description")
    gs.message("  " + "-" * 90)
    for k, (_img, _lbl, body, desc) in M3_CATALOG.items():
        gs.message(f"  {k:<28} {body:<6} {desc}")


def resolve_vims(vims_arg):
    """Resolve a vims= argument to an OPUS observation id.

    Accepts a catalog key (see VIMS_CATALOG) or a direct OPUS id (with or
    without a _vis/_ir channel suffix). Returns (opus_id, body_hint)."""
    a = vims_arg.strip()
    if a in VIMS_CATALOG:
        opus_id, body, _desc = VIMS_CATALOG[a]
        return opus_id, body
    if a.lower().startswith("co-vims-"):
        return a, None
    gs.fatal(f"Unknown VIMS key '{a}'. Use -l to list catalog keys, "
             "or pass a direct OPUS id (e.g. co-vims-v1799424623).")


def print_vims_catalog():
    gs.message("Cassini VIMS observations (use vims=<key>, or a direct OPUS id "
               "via opus_id=):")
    gs.message(f"  {'key':<22} {'body':<8} description")
    gs.message("  " + "-" * 90)
    for k, (_id, body, desc) in VIMS_CATALOG.items():
        gs.message(f"  {k:<22} {body:<8} {desc}")


def resolve_omega(omega_arg):
    """Resolve an omega= argument to (img_url, body_hint).

    Accepts a catalog key (see OMEGA_CATALOG) or a direct https URL to an
    attached-label OMEGA EDR *.QUB on archives.esac.esa.int -- there is no
    companion .LBL (label and data share one file)."""
    a = omega_arg.strip()
    if a in OMEGA_CATALOG:
        img_url, body, _desc = OMEGA_CATALOG[a]
        return img_url, body
    if a.lower().startswith(("http://", "https://")):
        if not a.upper().endswith(".QUB"):
            gs.fatal("Direct omega= URLs must point at an OMEGA EDR *.QUB file.")
        return a, None
    gs.fatal(f"Unknown OMEGA key '{a}'. Use -l to list catalog keys, "
             "or pass a direct https URL to an EDR *.QUB file.")


def print_omega_catalog():
    gs.message("Mars Express OMEGA EDR products (use omega=<key>, or a direct "
               "https URL to a *.QUB on archives.esac.esa.int):")
    gs.message(f"  {'key':<14} {'body':<6} description")
    gs.message("  " + "-" * 90)
    for k, (_img, body, desc) in OMEGA_CATALOG.items():
        gs.message(f"  {k:<14} {body:<6} {desc}")


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
    opt_crism        = options["crism"]
    opt_m3           = options["m3"]
    opt_vims         = options["vims"]
    opt_omega        = options["omega"]
    opt_opus         = options["opus"]
    opt_opus_id      = options["opus_id"]
    opt_vims_channel = options["vims_channel"] or "vis"
    opt_product      = options["product"] or "auto"
    opt_output       = options["output"]
    opt_band         = int(options["band"])
    opt_limit        = int(options["limit"])
    opt_download_dir = options["download_dir"]
    opt_project      = options["project"]
    flag_list        = flags["l"]
    flag_keep        = flags["k"]
    flag_override    = flags["o"]
    flag_noregion    = flags["r"]
    flag_spice       = flags["s"]
    flag_geom        = flags["g"]

    # ── COG / CRISM / M3 / VIMS catalog listing / import (independent of
    # STAC/PDS/OPUS) ──
    if flag_list and not any((opt_doi, opt_lid, opt_search, opt_opus, opt_opus_id)):
        print_cog_catalog()
        print_crism_catalog()
        print_m3_catalog()
        print_vims_catalog()
        print_omega_catalog()
        if not any((opt_cog, opt_crism, opt_m3, opt_vims, opt_omega)):
            return

    if opt_crism:
        if any((opt_doi, opt_lid, opt_search, opt_cog, opt_m3, opt_vims, opt_omega, opt_opus, opt_opus_id)):
            gs.fatal("crism= cannot be combined with doi=/lid=/search=/cog=/m3=/vims=/omega=/opus=/opus_id=.")
        if flag_list:
            return
        if not opt_output:
            gs.fatal("output= is required to import a CRISM product.")
        img_url, lbl_url, body_hint = resolve_crism(opt_crism)
        gs.message(f"CRISM source: {img_url}")

        body_slug = (body_hint or _infer_body_from_url(img_url) or "mars")
        local_img = _rsdata_dest(img_url, body_hint)
        local_lbl = os.path.join(os.path.dirname(local_img),
                                  os.path.basename(urllib.parse.urlparse(lbl_url).path))
        _wget_resumable(img_url, local_img)
        gs.message(f"Fetching label: {lbl_url}")
        _wget_resumable(lbl_url, local_lbl)

        nl, ns = _pds3_image_shape(local_lbl)
        if nl and ns:
            gs.run_command("g.region", n=nl, s=0, e=ns, w=0,
                           nsres=1, ewres=1, quiet=True)

        gs.message("Importing CRISM TRDR cube via p.in.pds3 …")
        gs.run_command("p.in.pds3",
                       flags="go" if flag_override else "g",
                       input=local_lbl, output=opt_output, overwrite=True)
        # Multi-band cube: p.in.pds3 -g writes <output>.1 .. <output>.N and
        # groups them under <output>; align the region to band 1.
        _align_region_to_raster(f"{opt_output}.1", save_default=False)

        # Detector-specific sensor id (S = VNIR, L = IR detector, per the
        # real CRISM TRDR filename convention, e.g. "..._IF156S_TRR3.IMG"
        # vs "..._IF156L_TRR3.IMG") -- lets p.phocube -c auto-detect
        # instrument= from this metadata instead of requiring it by hand.
        sensor = "MRO_CRISM"
        m = re.search(r"_[A-Za-z0-9]*([SL])_TRR\d*\.IMG$", img_url, re.IGNORECASE)
        if m:
            sensor = "MRO_CRISM_VNIR" if m.group(1).upper() == "S" else "MRO_CRISM_IR"

        # p.in.pds3 -g already wrote planetary.json for <output>.1 (generic
        # sensor metadata); write_planetary_metadata() is create-only and
        # would silently skip here, so update the existing record in place
        # instead -- this is what actually makes the detector-specific
        # sensor= reach p.phocube's auto-detection.
        if p_meta.PlanetaryMetadata.exists(f"{opt_output}.1"):
            meta = p_meta.PlanetaryMetadata.load(f"{opt_output}.1")
            meta.sensor = sensor
            meta.mission = "MRO"
            meta.body = body_slug.upper()
            meta.source_file = img_url
            meta.add_history_entry(" ".join(sys.argv))
            meta.save(f"{opt_output}.1")
        else:
            p_meta.write_planetary_metadata(
                f"{opt_output}.1",
                module="p.in.archive",
                command=" ".join(sys.argv),
                data_type="image",
                sensor=sensor,
                mission="MRO",
                body=body_slug.upper(),
                source_file=img_url,
            )
        gs.message(f"Imported CRISM TRDR cube as imagery group '{opt_output}' "
                   f"(bands '{opt_output}.1', '{opt_output}.2', ...).")

        if flag_spice:
            _attach_crism_spice(local_lbl, f"{opt_output}.1", body_slug)
        return

    if opt_m3:
        if any((opt_doi, opt_lid, opt_search, opt_cog, opt_crism, opt_vims, opt_omega, opt_opus, opt_opus_id)):
            gs.fatal("m3= cannot be combined with doi=/lid=/search=/cog=/crism=/vims=/omega=/opus=/opus_id=.")
        if flag_list:
            return
        if not opt_output:
            gs.fatal("output= is required to import an M3 product.")
        img_url, lbl_url, body_hint = resolve_m3(opt_m3)
        gs.message(f"M3 source: {img_url}")

        body_slug = (body_hint or _infer_body_from_url(img_url) or "moon")
        local_img = _rsdata_dest(img_url, body_hint)
        local_lbl = os.path.join(os.path.dirname(local_img),
                                  os.path.basename(urllib.parse.urlparse(lbl_url).path))
        _wget_resumable(img_url, local_img)
        gs.message(f"Fetching label: {lbl_url}")
        _wget_resumable(lbl_url, local_lbl)

        nl, ns = _pds3_image_shape(local_lbl)
        if nl and ns:
            gs.run_command("g.region", n=nl, s=0, e=ns, w=0,
                           nsres=1, ewres=1, quiet=True)

        gs.message("Importing M3 L1B cube via p.in.pds3 …")
        gs.run_command("p.in.pds3",
                       flags="go" if flag_override else "g",
                       input=local_lbl, output=opt_output, overwrite=True)
        # Multi-band cube: p.in.pds3 -g writes <output>.1 .. <output>.N and
        # groups them under <output>; align the region to band 1.
        _align_region_to_raster(f"{opt_output}.1", save_default=False)
        p_meta.write_planetary_metadata(
            f"{opt_output}.1",
            module="p.in.archive",
            command=" ".join(sys.argv),
            data_type="image",
            sensor="CH1_M3",
            mission="CHANDRAYAAN-1",
            body=body_slug.upper(),
            source_file=img_url,
        )
        gs.message(f"Imported M3 L1B cube as imagery group '{opt_output}' "
                   f"(bands '{opt_output}.1', '{opt_output}.2', ...).")

        if flag_geom:
            _attach_m3_geometry(local_lbl, img_url, opt_output, body_slug)
        return

    if opt_vims:
        if any((opt_doi, opt_lid, opt_search, opt_cog, opt_crism, opt_m3, opt_omega, opt_opus, opt_opus_id)):
            gs.fatal("vims= cannot be combined with doi=/lid=/search=/cog=/crism=/m3=/omega=/opus=/opus_id=.")
        if flag_list:
            return
        if not opt_output:
            gs.fatal("output= is required to import a VIMS product.")
        opus_id, _body_hint = resolve_vims(opt_vims)
        gs.message(f"VIMS source: OPUS ID {opus_id}")
        # Delegate to the existing opus_id= path below, which already
        # knows how to fetch/import a VIMS .qub via opus_files()/p.in.pds3
        # (including real-body inference from the resulting download URL).
        opt_opus_id = opus_id
        # fall through into the OPUS branch below

    if opt_omega:
        if any((opt_doi, opt_lid, opt_search, opt_cog, opt_crism, opt_m3, opt_vims, opt_opus, opt_opus_id)):
            gs.fatal("omega= cannot be combined with doi=/lid=/search=/cog=/crism=/m3=/vims=/opus=/opus_id=.")
        if flag_list:
            return
        if not opt_output:
            gs.fatal("output= is required to import an OMEGA product.")
        img_url, body_hint = resolve_omega(opt_omega)
        gs.message(f"OMEGA source: {img_url}")

        body_slug = (body_hint or _infer_body_from_url(img_url) or "mars")
        local_img = _rsdata_dest(img_url, body_hint)
        _wget_resumable(img_url, local_img)

        gs.message("Importing OMEGA EDR cube via p.in.pds3 …")
        gs.run_command("p.in.pds3",
                       flags="go" if flag_override else "g",
                       input=local_img, output=opt_output, overwrite=True)
        # Multi-band cube: p.in.pds3 -g writes <output>.1 .. <output>.N and
        # groups them under <output>; align the region to band 1.
        _align_region_to_raster(f"{opt_output}.1", save_default=False)
        # p.in.pds3 -g already wrote planetary.json for <output>.1 (generic
        # sensor="OMEGA" from the label's INSTRUMENT_ID); write_planetary_metadata()
        # is create-only and would silently skip here, so update the existing
        # record in place instead, same fix as the crism= path above.
        if p_meta.PlanetaryMetadata.exists(f"{opt_output}.1"):
            meta = p_meta.PlanetaryMetadata.load(f"{opt_output}.1")
            meta.sensor = "MEX_OMEGA"
            meta.mission = "MARS EXPRESS"
            meta.body = body_slug.upper()
            meta.source_file = img_url
            meta.add_history_entry(" ".join(sys.argv))
            meta.save(f"{opt_output}.1")
        else:
            p_meta.write_planetary_metadata(
                f"{opt_output}.1",
                module="p.in.archive",
                command=" ".join(sys.argv),
                data_type="image",
                sensor="MEX_OMEGA",
                mission="MARS EXPRESS",
                body=body_slug.upper(),
                source_file=img_url,
            )
        gs.message(f"Imported OMEGA EDR cube as imagery group '{opt_output}' "
                   f"(bands '{opt_output}.1', '{opt_output}.2', ...).")
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
                module="p.in.archive",
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
        if any((opt_doi, opt_lid, opt_search, opt_cog, opt_crism)):
            gs.fatal("opus= / opus_id= cannot be combined with "
                     "doi=, lid=, search=, cog=, or crism=.")
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
            file_list = opus_files(opus_id, channel=opt_vims_channel)
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
            file_list = opus_files(opus_id, channel=opt_vims_channel)
            if not file_list:
                gs.fatal(f"No downloadable files for OPUS ID '{opus_id}'.")

        # Determine which file to download (.qub for VIMS, .img for ISS).
        dl_url, dl_fname = _pick_raw_product(file_list, opt_vims_channel, opt_product)
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
        # Prefer the LBL whose base name matches the selected data file
        # (e.g. _CALIB.LBL for _CALIB.IMG) over any other .lbl in the list.
        dl_base_lower = os.path.splitext(dl_fname.lower())[0]
        lbl_url = lbl_fname = None
        for url, fname, _pt in file_list:
            if fname.lower().endswith(".lbl") and os.path.splitext(fname.lower())[0] == dl_base_lower:
                lbl_url = url
                lbl_fname = os.path.join(os.path.dirname(dest), fname)
                break
        if not lbl_url:
            for url, fname, _pt in file_list:
                if fname.lower().endswith(".lbl"):
                    lbl_url = url
                    lbl_fname = os.path.join(os.path.dirname(dest), fname)
                    break
        if lbl_url:
            gs.message(f"Fetching label: {lbl_url}")
            _wget_resumable(lbl_url, lbl_fname)

        # VIMS .qub labels reference shared "structure" files by relative
        # filename (^STRUCTURE = "core_description.fmt", etc.) for the
        # CORE_ITEM_BYTES/CORE_ITEM_TYPE/CORE_NULL/... keywords that
        # describe how to actually read the pixel data -- without them
        # p.in.pds3 silently falls back to a wrong default (8-bit
        # unsigned) instead of the real 16-bit signed DN. OPUS already
        # enumerates these alongside the .qub/.lbl; fetch any of them too,
        # into the same local directory so p.in.pds3 finds them via the
        # label's own relative path.
        if dl_fname.lower().endswith(".qub"):
            for url, fname, _pt in file_list:
                if fname.lower().endswith(".fmt"):
                    fmt_dest = os.path.join(os.path.dirname(dest), fname)
                    gs.message(f"Fetching structure file: {url}")
                    _wget_resumable(url, fmt_dest)

        gs.message(f"Fetching cube ({opt_vims_channel.upper()}): {dl_url}")
        _wget_resumable(dl_url, dest)

        # Import via p.in.pds3 (handles both .img PDS3 images and .qub cubes).
        ext = os.path.splitext(dl_fname.lower())[1]
        if ext == ".qub":
            kind = f"VIMS {opt_vims_channel.upper()} cube"
        else:
            kind = "PDS3 image"
        # For detached-label PDS3 files (.lbl + .img), pass the .lbl so
        # p_pds can resolve the data pointer correctly.
        import_path = lbl_fname if (lbl_fname and os.path.isfile(lbl_fname)) else dest
        # Set region to the native image dimensions before importing so
        # p.in.pds3 doesn't clip/pad to a stale ring-plane region.
        if import_path.lower().endswith(".lbl"):
            nl, ns = _pds3_image_shape(import_path)
            if nl and ns:
                gs.run_command("g.region", n=nl, s=0, e=ns, w=0,
                               nsres=1, ewres=1, quiet=True)
        gs.message(f"Importing {kind} via p.in.pds3 …")
        # VIMS .qub cubes are multi-band (352 IR / 96 VIS); register them in
        # an imagery group, same convention as crism=/m3=. Single-band ISS
        # .img products don't need one.
        pds3_flags = ("g" if ext == ".qub" else "") + ("o" if flag_override else "")
        gs.run_command("p.in.pds3",
                       flags=pds3_flags,
                       input=import_path, output=opt_output, overwrite=True)
        # CISSCAL calibrated images use -1.91e+38 as an invalid-pixel sentinel.
        # p.in.pds3 imports those as real floats; convert to GRASS NULL so
        # bilinear interpolation in p.in.rings is not contaminated.
        if dl_fname.lower().endswith(".img") and "_calib" in dl_fname.lower():
            gs.message("Nulling CISSCAL sentinel values (< -1) …")
            gs.run_command("r.mapcalc",
                           expression=f"{opt_output} = if({opt_output} < -1, null(), {opt_output})",
                           overwrite=True, quiet=True)
        _align_region_to_raster(f"{opt_output}.1" if ext == ".qub" else opt_output,
                                 save_default=False)

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

        # ISS NAC/WAC's p.phocube -c camera model needs the real filter
        # pair (no single focal length is correct -- it varies per filter
        # combination, e.g. INS-82360_CL1_CL2_FOCAL_LENGTH) -- read it
        # from the real label, same convention p_meta.filter_name already
        # documents ("F1/F2").
        _filter_val = None
        if _sensor in ("CASSINI_ISS_NAC", "CASSINI_ISS_WAC") and lbl_fname:
            _filter_val = _pds3_filter_pair(lbl_fname)

        # VIMS's p.phocube -c instrument=VIMS_IR/VIMS_VIS camera model
        # needs the real per-cube SamplingMode/XOffset/ZOffset/SwathWidth/
        # SwathLength -- none of these live in any SPICE kernel, only the
        # PDS3 label's Instrument group (see TODO.md).
        _vims_geom = {}
        if _sensor == "CASSINI_VIMS" and lbl_fname:
            _vims_geom = _pds3_vims_geometry(lbl_fname)

        # p.in.pds3 already wrote planetary.json for this map (generic
        # sensor/mission from the label itself); write_planetary_metadata()
        # is create-only and would silently skip here, so update the
        # existing record in place instead, same fix as crism=/m3=/omega=.
        _meta_target = f"{opt_output}.1" if ext == ".qub" else opt_output
        if p_meta.PlanetaryMetadata.exists(_meta_target):
            meta = p_meta.PlanetaryMetadata.load(_meta_target)
            if _sensor:     meta.sensor = _sensor
            meta.mission = "CASSINI"
            if _body_val:   meta.body = _body_val
            if _filter_val: meta.filter_name = _filter_val
            for k, v in _vims_geom.items():
                setattr(meta, k, v)
            meta.pds_product_id = opus_id
            meta.source_file = dl_url
            meta.add_history_entry(" ".join(sys.argv))
            meta.save(_meta_target)
        else:
            p_meta.write_planetary_metadata(
                _meta_target,
                module="p.in.archive",
                command=" ".join(sys.argv),
                data_type="image",
                sensor=_sensor,
                mission="CASSINI",
                body=_body_val,
                filter_name=_filter_val,
                **_vims_geom,
                pds_product_id=opus_id,
                source_file=dl_url,
            )
        if ext == ".qub":
            gs.message(f"Imported {kind} as imagery group '{opt_output}' "
                       f"('{opt_output}.1' .. '{opt_output}.N').")
        else:
            gs.message(f"Imported {kind} as '{opt_output}'.")
        return

    # Validate: exactly one of doi/lid/search
    n_src = sum(1 for x in (opt_doi, opt_lid, opt_search) if x)
    if n_src == 0:
        gs.fatal("Provide exactly one of doi=, lid=, search=, cog=, crism=, "
                 "opus=, or opus_id= (or -l to list options).")
    if n_src > 1:
        gs.fatal("Provide exactly one of doi=, lid=, or search= "
                 "(got multiple).")
    if not flag_list and not opt_output:
        gs.fatal("output= is required unless -l (list only) is given.")

    dest_dir = opt_download_dir if opt_download_dir else tempfile.mkdtemp(prefix="p_in_archive_")
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
            module="p.in.archive",
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
