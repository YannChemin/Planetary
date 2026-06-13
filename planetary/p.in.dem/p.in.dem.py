#!/usr/bin/env python3
############################################################################
# MODULE:       p.in.dem
# PURPOSE:      Import and prepare a planetary DEM into GRASS GIS.
#               Handles multi-tile mosaicking, void filling, and resampling.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Import and prepare a planetary DEM (single tile or mosaic).
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: DEM
# %end

# %option G_OPT_F_INPUT
# % key: input
# % label: Input DEM file(s): path, comma list, or glob
# % description: PDS3 .lbl/.img, ISIS3 .cub, GeoTIFF. Multiple tiles are mosaicked.
# % required: yes
# %end

# %option G_OPT_R_OUTPUT
# % description: Name for the output DEM raster map
# % required: yes
# %end

# %option
# % key: body
# % type: string
# % label: Path to body descriptor JSON
# % description: Provides planetary radius and projection information.
# % required: no
# %end

# %option
# % key: resolution
# % type: double
# % label: Target resolution in map units (metres)
# % description: If omitted the native resolution of the first tile is kept.
# % required: no
# %end

# %option
# % key: resample
# % type: string
# % options: nearest,bilinear,bicubic,lanczos,bilinear_f,bicubic_f
# % answer: bilinear_f
# % label: Resampling method used during reprojection and rescaling
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % answer: 300
# % label: Maximum memory to use in MB
# % required: no
# %end

# %flag
# % key: f
# % description: Fill null/void pixels after import (uses r.fill.stats)
# %end

# %flag
# % key: r
# % description: Set computational region to match the output DEM
# %end

# %flag
# % key: k
# % description: Keep intermediate per-tile rasters
# %end

import os
import sys
import glob
import shutil
import tempfile
import atexit

import grass.script as gs
from grass.exceptions import CalledModuleError

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import body_params, init_tmp
import p_meta

_tmpdir = None
_tiles  = []


def _cleanup():
    if _tmpdir and os.path.isdir(_tmpdir):
        shutil.rmtree(_tmpdir, ignore_errors=True)


atexit.register(_cleanup)


def resolve_inputs(raw):
    """Expand comma list and globs to a sorted list of file paths."""
    files = []
    for part in raw.split(","):
        part = part.strip()
        expanded = glob.glob(os.path.expandvars(os.path.expanduser(part)))
        if expanded:
            files.extend(sorted(expanded))
        elif os.path.isfile(part):
            files.append(part)
        else:
            gs.warning(f"No file matches: {part}")
    return files


def import_one(fpath, mapname, resample, memory):
    """Import a single DEM tile into a GRASS raster map."""
    ext = os.path.splitext(fpath)[1].lower()
    gdal_input = fpath

    # For PDS3 .img, locate companion .lbl
    if ext in (".img", ".raw"):
        base = os.path.splitext(fpath)[0]
        for suf in (".lbl", ".LBL"):
            if os.path.isfile(base + suf):
                gdal_input = base + suf
                break

    gs.run_command(
        "r.import",
        input=gdal_input,
        output=mapname,
        resample=resample,
        memory=memory,
        quiet=True,
        overwrite=True,
    )


def auto_scale_km(mapname):
    """
    Detect km-valued DEMs by checking if max value < 50 (Moon/Mars max ~30 km).
    If likely km, multiply by 1000 to convert to metres.
    """
    stats = gs.parse_command("r.univar", map=mapname, flags="g", quiet=True)
    vmax = abs(float(stats.get("max", 0)))
    if vmax < 50:
        gs.message(f"  Values look like km (max={vmax:.2f}); converting to metres.")
        tmp = mapname + "_km"
        gs.run_command("g.rename", raster=f"{mapname},{tmp}", quiet=True)
        gs.mapcalc(f"{mapname} = {tmp} * 1000.0", overwrite=True, quiet=True)
        gs.run_command("g.remove", type="raster", name=tmp, flags="f", quiet=True)
        gs.run_command("r.support", map=mapname, units="meters", quiet=True)


def main():
    global _tmpdir, _tiles

    opt_input   = options["input"]
    opt_output  = options["output"]
    opt_body    = options["body"]
    opt_res     = options["resolution"]
    opt_resamp  = options["resample"]
    opt_memory  = int(options["memory"])
    flag_fill   = flags["f"]
    flag_region = flags["r"]
    flag_keep   = flags["k"]

    _tmpdir = tempfile.mkdtemp(prefix="pin_dem_")

    files = resolve_inputs(opt_input)
    if not files:
        gs.fatal("No input files found.")

    gs.message(f"Found {len(files)} input file(s).")

    # ── import each tile ────────────────────────────────────────────────
    tile_names = []
    for i, fpath in enumerate(files):
        tname = f"pin_dem_tile_{i:03d}_{os.getpid()}"
        gs.message(f"  Importing tile {i+1}/{len(files)}: {os.path.basename(fpath)}")
        try:
            import_one(fpath, tname, opt_resamp, opt_memory)
        except CalledModuleError as e:
            gs.fatal(f"Failed to import {fpath}: {e}")
        auto_scale_km(tname)
        tile_names.append(tname)

    # ── mosaic if multiple tiles ─────────────────────────────────────────
    if len(tile_names) == 1:
        gs.run_command("g.rename",
                       raster=f"{tile_names[0]},{opt_output}",
                       quiet=True, overwrite=True)
    else:
        gs.message("Mosaicking tiles with r.patch…")
        gs.run_command(
            "r.patch",
            input=",".join(tile_names),
            output=opt_output,
            quiet=True,
            overwrite=gs.overwrite(),
        )
        if not flag_keep:
            gs.run_command("g.remove", type="raster",
                           name=",".join(tile_names), flags="f", quiet=True)

    # ── void fill ────────────────────────────────────────────────────────
    if flag_fill:
        gs.message("Filling null pixels (r.fill.stats)…")
        tmp_filled = f"pin_dem_filled_{os.getpid()}"
        gs.run_command(
            "r.fill.stats",
            input=opt_output,
            output=tmp_filled,
            mode="wmean",
            distance=3,
            quiet=True,
            overwrite=True,
        )
        gs.run_command("g.rename",
                       raster=f"{tmp_filled},{opt_output}",
                       quiet=True, overwrite=True)

    # ── resample to target resolution ────────────────────────────────────
    if opt_res:
        gs.message(f"Resampling to {opt_res} m…")
        gs.run_command("g.region", raster=opt_output, res=opt_res, quiet=True)
        tmp_res = f"pin_dem_resampled_{os.getpid()}"
        gs.run_command(
            "r.resamp.interp",
            input=opt_output,
            output=tmp_res,
            method=opt_resamp.replace("_f", ""),
            quiet=True,
            overwrite=True,
        )
        gs.run_command("g.rename",
                       raster=f"{tmp_res},{opt_output}",
                       quiet=True, overwrite=True)

    # ── metadata ─────────────────────────────────────────────────────────
    gs.run_command("r.support",
                   map=opt_output,
                   title="Planetary DEM",
                   history=f"Imported from: {opt_input}",
                   source1="p.in.dem",
                   units="meters",
                   quiet=True)

    if opt_body:
        try:
            bd = body_params(opt_body)
            gs.run_command("r.support", map=opt_output,
                           description=f"Body: {bd['name']}, "
                                       f"R={bd['semi_major_axis_m']/1000:.1f} km",
                           quiet=True)
        except Exception:
            pass

    p_meta.write_planetary_metadata(
        opt_output,
        module="p.in.dem",
        command=" ".join(sys.argv),
        data_type="dem",
        radiometric_quantity="elevation",
        radiometric_units="m",
        body=opt_body.upper() if opt_body else None,
        source_file=opt_input,
    )

    # ── region ───────────────────────────────────────────────────────────
    if flag_region:
        gs.run_command("g.region", raster=opt_output, quiet=True)
        gs.message(f"Computational region set to {opt_output}.")

    gs.message(f"DEM '{opt_output}' ready.")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
