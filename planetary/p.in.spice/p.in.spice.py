#!/usr/bin/env python3
############################################################################
# MODULE:       p.in.spice
# PURPOSE:      Fetch and manage NAIF SPICE kernels for the planetary suite,
#               and generate a meta-kernel the other p.* modules can load.
#               Auto-downloads a named kernel bundle from NAIF on first run,
#               or works from manually-placed kernels. Cache lives under the
#               GRASS user config directory.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org). The NAIF kernels themselves
#               are distributed under NASA/NAIF terms.
############################################################################

# %module
# % description: Download and manage NAIF SPICE kernels and build a meta-kernel for the planetary suite.
# % keyword: Planetary
# % keyword: SPICE & Ephemeris
# % keyword: import
# % keyword: kernels
# %end

# %option
# % key: bundle
# % type: string
# % label: Kernel bundle to install / build a meta-kernel for
# % description: moon-me = lunar mean-Earth frame (matches LOLA DEMs); moon-iau = IAU_MOON (text PCK only); mars / generic for other bodies.
# % options: moon-me,moon-iau,mars,generic
# % required: no
# %end

# %option G_OPT_M_DIR
# % key: dest
# % label: Override cache directory (default: <grass-config>/p_spice)
# % required: no
# %end

# %option
# % key: timeout
# % type: integer
# % label: Per-file download timeout (seconds)
# % answer: 600
# % required: no
# %end

# %flag
# % key: l
# % description: List available kernel bundles and exit
# %end

# %flag
# % key: d
# % description: Download missing kernels for the bundle from NAIF
# %end

# %flag
# % key: m
# % description: (Re)build the meta-kernel only, from kernels already present
# %end

# %flag
# % key: f
# % description: Force re-download even if a kernel file already exists
# %end

import os
import sys
import hashlib
import urllib.request

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_spice

NAIF = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels"

# Pinned SHA-256 of each kernel file (all are version-tagged, stable products).
# Verified after download; a mismatch is fatal. Files not listed here are
# downloaded without verification (a warning is issued).
SHA256 = {
    "naif0012.tls":                 "678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b",
    "pck00011.tpc":                 "3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1",
    "de440s.bsp":                   "c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2",
    "moon_080317.tf":               "78732477b96f9863e7b0d65bcee3c22b8707ca5ed0db56d1173319cb2e8c7993",
    "moon_pa_de421_1900-2050.bpc":  "656f90616403d75a75f0cd6c8830fc5b44f8cb4facb5ccb8915e752b397520cf",
}

# Kernel bundles.  Each entry: (relative-url-under-NAIF, local-filename).
# Files are loaded by the meta-kernel in listed order (text kernels first,
# binary orientation, then ephemeris SPK last) per SPICE convention.
BUNDLES = {
    "moon-iau": {
        "description": "Moon, IAU_MOON body-fixed frame (text PCK only — "
                       "lowest-precision orientation, no binary kernels).",
        "frame": "IAU_MOON",
        "kernels": [
            ("lsk/naif0012.tls",          "naif0012.tls"),
            ("pck/pck00011.tpc",          "pck00011.tpc"),
            ("spk/planets/de440s.bsp",    "de440s.bsp"),
        ],
    },
    "moon-me": {
        "description": "Moon, MOON_ME mean-Earth/polar-axis frame (DE421) — "
                       "matches LOLA/LRO DEM cartographic frame. Recommended.",
        "frame": "MOON_ME",
        "kernels": [
            ("lsk/naif0012.tls",                    "naif0012.tls"),
            ("pck/pck00011.tpc",                    "pck00011.tpc"),
            ("fk/satellites/moon_080317.tf",        "moon_080317.tf"),
            ("pck/moon_pa_de421_1900-2050.bpc",     "moon_pa_de421_1900-2050.bpc"),
            ("spk/planets/de440s.bsp",              "de440s.bsp"),
        ],
    },
    "mars": {
        "description": "Mars, IAU_MARS body-fixed frame (text PCK).",
        "frame": "IAU_MARS",
        "kernels": [
            ("lsk/naif0012.tls",          "naif0012.tls"),
            ("pck/pck00011.tpc",          "pck00011.tpc"),
            ("spk/planets/de440s.bsp",    "de440s.bsp"),
        ],
    },
    "generic": {
        "description": "Sun/planet barycentre ephemeris + planetary "
                       "constants (text PCK). IAU_<BODY> frames.",
        "frame": "IAU_<BODY>",
        "kernels": [
            ("lsk/naif0012.tls",          "naif0012.tls"),
            ("pck/pck00011.tpc",          "pck00011.tpc"),
            ("spk/planets/de440s.bsp",    "de440s.bsp"),
        ],
    },
}


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _verify(fn, path):
    """Check a downloaded/present file against its pinned SHA-256.
    Fatal on mismatch; warns (and returns) if no pinned hash is known."""
    want = SHA256.get(fn)
    got = _sha256(path)
    if want is None:
        gs.warning(f"{fn}: no pinned checksum; sha256={got}")
        return
    if got != want:
        gs.fatal(f"Checksum mismatch for {fn}\n  expected {want}\n  got      {got}\n"
                 f"Delete {path} and re-download, or check the NAIF source.")
    gs.verbose(f"{fn}: checksum OK")


def _list_bundles():
    gs.message("Available SPICE kernel bundles:")
    gs.message("")
    for name, b in BUNDLES.items():
        gs.message(f"  {name}")
        gs.message(f"      frame:   {b['frame']}")
        gs.message(f"      {b['description']}")
        gs.message(f"      kernels: {', '.join(fn for _, fn in b['kernels'])}")
        gs.message("")


def _download(url, dst, timeout):
    tmp = dst + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "p.in.spice/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        total = int(r.headers.get("Content-Length", 0))
        done = 0
        with open(tmp, "wb") as out:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
                done += len(chunk)
                if total:
                    gs.percent(done, total, 5)
    os.replace(tmp, dst)
    return done


def main():
    opt_bundle  = options["bundle"]
    opt_dest    = options["dest"]
    opt_timeout = int(options["timeout"])
    flag_list   = flags["l"]
    flag_dl     = flags["d"]
    flag_meta   = flags["m"]
    flag_force  = flags["f"]

    if flag_list:
        _list_bundles()
        return

    if not opt_bundle:
        gs.fatal("Specify bundle= (or use -l to list bundles).")

    bundle = BUNDLES[opt_bundle]

    # Resolve cache layout.
    if opt_dest:
        kdir = os.path.join(opt_dest, "kernels")
        mdir = os.path.join(opt_dest, "meta")
    else:
        kdir = p_spice.kernels_dir()
        mdir = p_spice.meta_dir()
    os.makedirs(kdir, exist_ok=True)
    os.makedirs(mdir, exist_ok=True)

    gs.message(f"Bundle:        {opt_bundle}  (frame {bundle['frame']})")
    gs.message(f"Kernel cache:  {kdir}")
    gs.message(f"Meta-kernels:  {mdir}")

    local_files = [os.path.join(kdir, fn) for _, fn in bundle["kernels"]]

    # ── download phase ────────────────────────────────────────────────────
    if flag_dl:
        for (rel, fn), dst in zip(bundle["kernels"], local_files):
            url = f"{NAIF}/{rel}"
            if os.path.isfile(dst) and not flag_force:
                _verify(fn, dst)   # catch a stale/corrupt existing file
                gs.message(f"  ✓ {fn} (already present, verified)")
                continue
            gs.message(f"  ↓ {fn}  ←  {url}")
            try:
                n = _download(url, dst, opt_timeout)
            except Exception as e:
                gs.fatal(f"Download of {fn} failed: {e}")
            _verify(fn, dst)
            gs.verbose(f"    {n} bytes")
    elif not flag_meta:
        gs.warning("Neither -d (download) nor -m (meta only) given; "
                   "checking for already-present kernels.")

    # ── verify presence ──────────────────────────────────────────────────
    missing = [fn for (_, fn), dst in zip(bundle["kernels"], local_files)
               if not os.path.isfile(dst)]
    if missing:
        gs.fatal("Missing kernels: " + ", ".join(missing) +
                 f".\nRun with -d to download, or place them in {kdir} "
                 "manually (see the manual for sources).")

    # Verify pinned checksums of all present files (catches manual/offline
    # placement of the wrong or corrupt file before building the meta-kernel).
    if not flag_dl:
        for (_, fn), dst in zip(bundle["kernels"], local_files):
            _verify(fn, dst)

    # ── build the meta-kernel ─────────────────────────────────────────────
    meta_path = os.path.join(mdir, f"{opt_bundle}.tm")
    load_list = "\n".join(
        f"      '$KERNELS/{fn}'" for _, fn in bundle["kernels"])
    meta = (
        "KPL/MK\n"
        f"\\begintext\n"
        f"  Meta-kernel for bundle '{opt_bundle}' (frame {bundle['frame']}).\n"
        f"  Generated by p.in.spice. Body-fixed frame to pass to the\n"
        f"  planetary modules: {bundle['frame']}.\n"
        f"\\begindata\n"
        f"  PATH_VALUES  = ( '{kdir}' )\n"
        f"  PATH_SYMBOLS = ( 'KERNELS' )\n"
        f"  KERNELS_TO_LOAD = (\n{load_list}\n  )\n"
        f"\\begintext\n"
    )
    with open(meta_path, "w") as f:
        f.write(meta)

    gs.message("")
    gs.message(f"Meta-kernel written: {meta_path}")
    gs.message("Activate it for the current mapset with:")
    gs.message(f"  p.spice.config meta={meta_path} -a")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
