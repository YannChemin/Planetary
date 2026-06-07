#!/usr/bin/env python3
############################################################################
# MODULE:       p.terrain.hazard
# PURPOSE:      Composite terrain hazard map combining slope, roughness,
#               local relief, crater density, and curvature into a single
#               normalised hazard score [0,1] plus a hard exclusion mask.
#               Wraps existing GRASS and addon modules; adds no duplicate logic.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Composite terrain hazard map for planetary landing sites.
# % keyword: Planetary
# % keyword: Terrain Analysis
# % keyword: hazard
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_R_INPUT
# % key: slope
# % label: Pre-computed slope raster (degrees) — computed if omitted
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: roughness
# % label: Pre-computed RMS roughness raster (metres) — computed if omitted
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: craters
# % label: Crater density raster — included if provided
# % required: no
# %end

# %option
# % key: weights
# % type: string
# % label: Comma-separated criterion weights: slope,roughness,relief,craters,curvature
# % answer: 0.40,0.25,0.15,0.10,0.10
# % required: no
# %end

# %option
# % key: slope_max
# % type: double
# % label: Hard slope exclusion threshold (degrees)
# % answer: 15.0
# % required: no
# %end

# %option
# % key: roughness_max
# % type: double
# % label: Hard roughness exclusion threshold (metres RMS)
# % answer: 1.0
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: hazard
# % required: no
# %end

# %option
# % key: nprocs
# % type: integer
# % label: OpenMP threads for r.slope.aspect / r.neighbors
# % answer: 1
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % label: Row-cache size in MB for r.slope.aspect / r.neighbors
# % answer: 300
# % required: no
# %end

import os
import sys
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import cleanup_prefix, normalize_raster

_PREFIX_TMP = "pterr_haz_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def _ensure_slope(dem, prefix, par=None):
    par = par or {}
    out = f"{prefix}slope_{os.getpid()}"
    gs.run_command("r.slope.aspect", elevation=dem, slope=out,
                   format="degrees", quiet=True, overwrite=True, **par)
    return out, True


def _ensure_roughness(dem, prefix, par=None):
    par = par or {}
    rms = f"{prefix}rms_{os.getpid()}"
    dem_mean = f"{prefix}dmean_{os.getpid()}"
    dem_var  = f"{prefix}dvar_{os.getpid()}"
    gs.run_command("r.neighbors", input=dem, output=dem_mean,
                   method="average", size=11, quiet=True, overwrite=True, **par)
    gs.mapcalc(f"{dem_var} = ({dem} - {dem_mean})^2",
               overwrite=True, quiet=True)
    gs.run_command("r.neighbors", input=dem_var, output=rms,
                   method="average", size=11, quiet=True, overwrite=True, **par)
    gs.mapcalc(f"{rms} = sqrt({rms})", overwrite=True, quiet=True)
    gs.run_command("g.remove", type="raster",
                   name=f"{dem_mean},{dem_var}", flags="f", quiet=True)
    return rms, True


def main():
    opt_dem        = options["dem"]
    opt_slope      = options["slope"]
    opt_roughness  = options["roughness"]
    opt_craters    = options["craters"]
    opt_weights    = options["weights"]
    opt_slope_max  = float(options["slope_max"])
    opt_rough_max  = float(options["roughness_max"])
    opt_pfx        = options["prefix"]
    opt_nprocs = int(options.get("nprocs", 1) or 1)
    opt_memory = int(options.get("memory", 300) or 300)
    _PAR = {"nprocs": opt_nprocs, "memory": opt_memory}

    weights = [float(w.strip()) for w in opt_weights.split(",")]
    w_slope, w_rough, w_relief, w_craters, w_curv = (weights + [0]*5)[:5]
    pid = os.getpid()

    # ── gather / compute sub-criteria ────────────────────────────────────
    own_slope = own_rough = False

    if opt_slope:
        slope = opt_slope
    else:
        gs.message("Computing slope…")
        slope, own_slope = _ensure_slope(opt_dem, _PREFIX_TMP, par=_PAR)

    if opt_roughness:
        roughness = opt_roughness
    else:
        gs.message("Computing RMS roughness (window=11)…")
        roughness, own_rough = _ensure_roughness(opt_dem, _PREFIX_TMP, par=_PAR)

    # Local relief via r.neighbors range
    gs.message("Computing local relief…")
    relief = f"{_PREFIX_TMP}relief_{pid}"
    gs.run_command("r.neighbors", input=opt_dem, output=relief,
                   method="range", size=11, quiet=True, overwrite=True, **_PAR)

    # Curvature via r.param.scale
    gs.message("Computing curvature…")
    curv = f"{_PREFIX_TMP}curv_{pid}"
    gs.run_command("r.param.scale", input=opt_dem, output=curv,
                   method="profc", size=9,
                   quiet=True, overwrite=True)
    # Absolute curvature as hazard
    curv_abs = f"{_PREFIX_TMP}curvabs_{pid}"
    gs.mapcalc(f"{curv_abs} = abs({curv})", overwrite=True, quiet=True)

    # ── normalise each criterion to [0,1] ────────────────────────────────
    norm_slope  = f"{_PREFIX_TMP}nslope_{pid}"
    norm_rough  = f"{_PREFIX_TMP}nrough_{pid}"
    norm_relief = f"{_PREFIX_TMP}nrelief_{pid}"
    norm_curv   = f"{_PREFIX_TMP}ncurv_{pid}"

    normalize_raster(slope,    norm_slope)
    normalize_raster(roughness, norm_rough)
    normalize_raster(relief,   norm_relief)
    normalize_raster(curv_abs, norm_curv)

    # ── weighted composite ────────────────────────────────────────────────
    gs.message("Building composite hazard score…")
    composite_out = f"{opt_pfx}_composite"

    if opt_craters:
        norm_crat = f"{_PREFIX_TMP}ncrat_{pid}"
        normalize_raster(opt_craters, norm_crat)
        expr = (f"{composite_out} = "
                f"{w_slope}*{norm_slope} + "
                f"{w_rough}*{norm_rough} + "
                f"{w_relief}*{norm_relief} + "
                f"{w_craters}*{norm_crat} + "
                f"{w_curv}*{norm_curv}")
        gs.run_command("g.remove", type="raster",
                       name=norm_crat, flags="f", quiet=True)
    else:
        total = w_slope + w_rough + w_relief + w_curv
        if total == 0:
            total = 1.0
        expr = (f"{composite_out} = "
                f"({w_slope}*{norm_slope} + "
                f"{w_rough}*{norm_rough} + "
                f"{w_relief}*{norm_relief} + "
                f"{w_curv}*{norm_curv}) / {total}")

    gs.mapcalc(expr, overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.colors", map=composite_out, color="plasma", quiet=True)
    gs.run_command("r.support", map=composite_out,
                   title="Terrain hazard composite [0=safe, 1=hazardous]",
                   source1="p.terrain.hazard", quiet=True)

    # ── hard exclusion mask ───────────────────────────────────────────────
    mask_out = f"{opt_pfx}_mask"
    gs.mapcalc(
        f"{mask_out} = if({slope} > {opt_slope_max} || "
        f"{roughness} > {opt_rough_max}, 1, 0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=mask_out,
                   title=f"Hard exclusion mask (slope>{opt_slope_max}° or RMS>{opt_rough_max}m)",
                   source1="p.terrain.hazard", quiet=True)

    # ── per-criterion output maps (for diagnostics) ───────────────────────
    for src, dst in [(norm_slope,  f"{opt_pfx}_slope"),
                     (norm_rough,  f"{opt_pfx}_roughness"),
                     (norm_relief, f"{opt_pfx}_relief"),
                     (norm_curv,   f"{opt_pfx}_curvature")]:
        gs.run_command("g.rename", raster=f"{src},{dst}",
                       quiet=True, overwrite=gs.overwrite())
        gs.run_command("r.colors", map=dst, color="plasma", quiet=True)

    # ── clean up internal temporaries ────────────────────────────────────
    tmps = [relief, curv, curv_abs]
    if own_slope:
        tmps.append(slope)
    if own_rough:
        tmps.append(roughness)
    gs.run_command("g.remove", type="raster",
                   name=",".join(tmps), flags="f", quiet=True)

    gs.message("Output maps:")
    for m in [f"{opt_pfx}_slope", f"{opt_pfx}_roughness",
              f"{opt_pfx}_relief", f"{opt_pfx}_curvature",
              composite_out, mask_out]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
