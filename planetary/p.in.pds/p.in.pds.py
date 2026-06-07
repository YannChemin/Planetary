#!/usr/bin/env python3
############################################################################
#
# MODULE:       p.in.pds
# AUTHOR(S):    Yann Chemin
# PURPOSE:      Import a PDS3/PDS4/ISIS3 planetary data file directly into
#               a GRASS GIS raster map.
#
#               Import chain (in order of preference):
#               1. GDAL PDS/PDS4/ISIS3 driver reads the file directly
#                  (works for most modern products with a .LBL label file)
#               2. If the user supplies a .IMG without a companion .LBL,
#                  or if GDAL cannot open the file, the module falls back
#                  to the ISIS3 pipeline:
#                  pds2isis  ->  (cam2map if -c flag)  ->  GDAL ISIS3 driver
#
#               Unit handling:
#               PDS labels may express values in kilometres (SLDEM2015, LOLA
#               RDR, MOLA).  The module reads the UNIT keyword from the PDS
#               label and applies a scaling factor so the output raster is
#               always in metres.  Pass scale=1 to suppress this behaviour.
#
# LICENSE:      Unlicense (https://unlicense.org)
#
#############################################################################

# %module
# % description: Import a PDS3/PDS4/ISIS3 planetary file into a GRASS raster map.
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: PDS
# % keyword: ISIS
# %end

# %option G_OPT_F_INPUT
# % key: input
# % label: Input PDS3/PDS4/ISIS3 file (.lbl, .img, .cub, .xml)
# % description: For .img files the companion .lbl is located automatically.
# % required: yes
# %end

# %option
# % key: image
# % type: string
# % label: Detached image file (PDS3 only)
# % description: Use when the image binary is in a different directory than the label.
# % required: no
# %end

# %option G_OPT_R_OUTPUT
# % description: Name for the output GRASS raster map
# % required: yes
# %end

# %option
# % key: band
# % type: integer
# % label: Band number to import (1-based)
# % description: For multi-band ISIS cubes or PDS4 products.
# % answer: 1
# % required: no
# %end

# %option
# % key: isis3
# % type: string
# % label: Path to ISIS3 bin directory
# % description: Fallback when GDAL cannot open the file. Defaults to $ISISROOT/bin.
# % required: no
# %end

# %option
# % key: target
# % type: string
# % label: ISIS3 target body name (for cam2map, e.g. MOON, MARS)
# % description: Only needed when the -c flag is used.
# % required: no
# %end

# %option
# % key: scale
# % type: double
# % label: Multiplicative scale factor (0 = auto-detect from PDS UNIT keyword)
# % description: Use to convert units, e.g. km to m: scale=1000. 0 = auto.
# % answer: 0
# % required: no
# %end

# %option
# % key: resample
# % type: string
# % label: Resampling method for reprojection
# % options: nearest,bilinear,bicubic,lanczos,bilinear_f,bicubic_f
# % answer: bilinear_f
# % descriptions: nearest;nearest neighbour (categorical data);bilinear;bilinear interpolation;bicubic;bicubic interpolation;lanczos;Lanczos filter;bilinear_f;bilinear with fallback;bicubic_f;bicubic with fallback
# % required: no
# % guisection: Output
# %end

# %option
# % key: memory
# % type: integer
# % label: Maximum memory to use in MB
# % answer: 300
# % required: no
# % guisection: Output
# %end

# %flag
# % key: c
# % description: Run cam2map before import (for raw camera-geometry ISIS cubes)
# %end

# %flag
# % key: k
# % description: Keep intermediate ISIS cube and GeoTIFF files (for debugging)
# %end

# %flag
# % key: r
# % description: Set computational region to match imported map
# %end

import os
import sys
import re
import shutil
import subprocess
import tempfile
import atexit

import grass.script as gs
from grass.exceptions import CalledModuleError

# ── cleanup registry ────────────────────────────────────────────────────────

_tmpdir = None
_keep = False


def _cleanup():
    if _tmpdir and not _keep and os.path.isdir(_tmpdir):
        shutil.rmtree(_tmpdir, ignore_errors=True)


atexit.register(_cleanup)

# ── helpers ─────────────────────────────────────────────────────────────────

_KM_KEYWORDS = {"kilometer", "kilometres", "kilometer", "km"}
_M_KEYWORDS  = {"meter", "metre", "meters", "metres", "m"}


def _find_companion_lbl(img_path):
    """Return the .lbl/.LBL companion for a .img/.IMG file, or None."""
    base, ext = os.path.splitext(img_path)
    for suffix in (".lbl", ".LBL", ".Lbl"):
        candidate = base + suffix
        if os.path.isfile(candidate):
            return candidate
    return None


def _gdal_open(path):
    """Return True if GDAL can open the file, else False."""
    from osgeo import gdal
    gdal.UseExceptions()
    try:
        ds = gdal.Open(path)
        ok = ds is not None
        ds = None
        return ok
    except Exception:
        return False


def _read_pds_unit(label_path):
    """
    Parse the UNIT keyword from a PDS3 label file.
    Returns a float scale factor to convert to metres:
      km -> 1000.0
      m  -> 1.0
      otherwise -> 1.0
    """
    if not label_path or not os.path.isfile(label_path):
        return 1.0
    try:
        text = open(label_path).read()
    except OSError:
        return 1.0
    # Match UNIT = KILOMETER or UNIT = "KILOMETER" etc.
    m = re.search(r'UNIT\s*=\s*["\']?(\w+)', text, re.IGNORECASE)
    if not m:
        return 1.0
    unit = m.group(1).lower()
    if unit in _KM_KEYWORDS:
        gs.verbose(f"PDS label reports UNIT={m.group(1)}: will multiply by 1000 to convert to metres.")
        return 1000.0
    if unit in _M_KEYWORDS:
        return 1.0
    gs.warning(f"Unknown UNIT '{m.group(1)}' in PDS label; no scaling applied.")
    return 1.0


def _find_isis3_bin(user_path=None):
    """Locate the ISIS3 bin directory or raise."""
    if user_path and os.path.isdir(user_path):
        return user_path
    isisroot = os.environ.get("ISISROOT", "")
    candidate = os.path.join(isisroot, "bin")
    if os.path.isdir(candidate):
        return candidate
    # fall back to PATH
    if shutil.which("pds2isis"):
        return os.path.dirname(shutil.which("pds2isis"))
    gs.fatal(
        "Cannot locate ISIS3 bin directory. "
        "Set $ISISROOT or supply isis3= parameter."
    )


def _run_isis3(cmd_args, isis3_bin, env=None):
    """Run an ISIS3 app, raise on non-zero exit."""
    exe = os.path.join(isis3_bin, cmd_args[0])
    full = [exe] + cmd_args[1:]
    gs.verbose("Running: " + " ".join(full))
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    # ISIS3 needs ISISROOT set
    if "ISISROOT" not in merged_env:
        merged_env["ISISROOT"] = os.path.dirname(isis3_bin)
    r = subprocess.run(full, env=merged_env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True)
    if r.returncode != 0:
        gs.fatal(f"{cmd_args[0]} failed:\n{r.stderr.strip()}")
    gs.verbose(r.stdout)


def _import_gdal(src, output, band, resample, memory):
    """Import src (readable by GDAL) into a GRASS raster map."""
    # r.import handles reprojection if the CRS differs from the location
    gs.run_command(
        "r.import",
        input=src,
        band=band,
        output=output,
        resample=resample,
        memory=memory,
        quiet=True,
        overwrite=gs.overwrite(),
    )


def _apply_scale(output, scale_factor, tmpdir):
    """Multiply the raster by scale_factor in-place via r.mapcalc."""
    tmp = output + "_pin_pds_unscaled"
    gs.run_command("g.rename", raster=f"{output},{tmp}", quiet=True)
    gs.mapcalc(f"{output} = {tmp} * {scale_factor}", overwrite=True, quiet=True)
    gs.run_command("g.remove", type="raster", name=tmp, flags="f", quiet=True)


def _write_history(output, src):
    """Tag the map with source provenance."""
    gs.run_command(
        "r.support",
        map=output,
        history=f"Imported from: {src}",
        source1="p.in.pds",
        quiet=True,
    )

# ── main ────────────────────────────────────────────────────────────────────

def main():
    global _tmpdir, _keep

    opt_input   = options["input"]
    opt_image   = options["image"]
    opt_output  = options["output"]
    opt_band    = int(options["band"])
    opt_isis3   = options["isis3"]
    opt_target  = options["target"]
    opt_scale   = float(options["scale"])
    opt_resample= options["resample"]
    opt_memory  = int(options["memory"])

    flag_cammap = flags["c"]
    flag_keep   = flags["k"]
    flag_region = flags["r"]

    _keep = flag_keep

    _tmpdir = tempfile.mkdtemp(prefix="pin_pds_")
    gs.verbose(f"Temporary directory: {_tmpdir}")

    # ── 1. Resolve the input file ────────────────────────────────────────

    input_path = os.path.abspath(opt_input)
    if not os.path.isfile(input_path):
        gs.fatal(f"Input file not found: {input_path}")

    ext = os.path.splitext(input_path)[1].lower()
    label_path = None   # .lbl companion, when known

    # If a bare .img was given, look for the companion .lbl
    if ext in (".img", ".raw"):
        companion = _find_companion_lbl(input_path)
        if companion:
            gs.verbose(f"Found companion label: {companion}")
            label_path = companion
            gdal_input = companion        # GDAL PDS driver reads via the label
        else:
            gs.verbose("No companion .lbl found; will try GDAL on .img directly.")
            gdal_input = input_path
    elif ext in (".lbl",):
        label_path = input_path
        gdal_input = input_path
    else:
        # .cub (ISIS3), .xml (PDS4), .tif, etc.
        gdal_input = input_path

    # ── 2. Determine unit scale factor ─────────────────────────────────

    if opt_scale == 0.0:
        auto_scale = _read_pds_unit(label_path or (input_path if ext == ".lbl" else None))
    else:
        auto_scale = opt_scale

    # ── 3. Try the GDAL fast path ───────────────────────────────────────

    gdal_ok = _gdal_open(gdal_input)

    if gdal_ok and not flag_cammap:
        gs.message(f"Importing via GDAL driver (band {opt_band})…")
        _import_gdal(gdal_input, opt_output, opt_band, opt_resample, opt_memory)

    else:
        # ── 4. ISIS3 fallback path ─────────────────────────────────────

        if not gdal_ok:
            gs.message("GDAL cannot open the file; falling back to ISIS3 pipeline…")
        else:
            gs.message("cam2map requested; using ISIS3 pipeline…")

        isis3_bin = _find_isis3_bin(opt_isis3)

        # 4a. pds2isis: PDS → ISIS cube
        cub_path = os.path.join(_tmpdir, "pin_pds_import.cub")
        pds2isis_args = [
            "pds2isis",
            f"from={input_path}",
            f"to={cub_path}",
        ]
        if opt_image:
            pds2isis_args.append(f"image={opt_image}")
        _run_isis3(pds2isis_args, isis3_bin)

        # 4b. cam2map: raw camera → map projection (optional)
        if flag_cammap:
            mapped_cub = os.path.join(_tmpdir, "pin_pds_mapped.cub")
            cam2map_args = ["cam2map", f"from={cub_path}", f"to={mapped_cub}"]
            if opt_target:
                cam2map_args.append(f"target={opt_target}")
            _run_isis3(cam2map_args, isis3_bin)
            cub_path = mapped_cub

        # 4c. GDAL reads the ISIS3 cube directly
        gs.message("Importing ISIS3 cube via GDAL ISIS3 driver…")
        gdal_src = cub_path
        if opt_band > 1:
            gdal_src = f"{cub_path}+{opt_band}"   # GDAL band selection syntax
        _import_gdal(gdal_src, opt_output, 1, opt_resample, opt_memory)

    # ── 5. Apply unit scale factor (km → m etc.) ────────────────────────

    if auto_scale != 1.0:
        gs.message(f"Applying scale factor {auto_scale} (unit conversion to metres)…")
        _apply_scale(opt_output, auto_scale, _tmpdir)
        gs.run_command(
            "r.support", map=opt_output,
            units="meters",
            quiet=True,
        )

    # ── 6. Metadata and history ─────────────────────────────────────────

    _write_history(opt_output, opt_input)

    # ── 7. Optionally set region to the imported map ─────────────────────

    if flag_region:
        gs.run_command("g.region", raster=opt_output, quiet=True)
        gs.message(f"Computational region set to {opt_output}.")

    gs.message(f"Raster map '{opt_output}' created.")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
