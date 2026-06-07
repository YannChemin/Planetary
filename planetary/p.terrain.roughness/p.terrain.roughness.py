#!/usr/bin/env python3
############################################################################
# MODULE:       p.terrain.roughness
# PURPOSE:      Compute terrain roughness metrics in a sliding window:
#               RMS plane-detrended roughness, coefficient of variation of
#               slope, and local Moran's I spatial autocorrelation.
#               Complements r.roughness.vector and r.tri without duplicating them.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Sliding-window roughness metrics for planetary landing (RMS, CV, Moran's I).
# % keyword: Planetary
# % keyword: Terrain Analysis
# % keyword: roughness
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_R_INPUT
# % key: slope
# % label: Pre-computed slope raster (degrees) — computed from dem if omitted
# % required: no
# %end

# %option
# % key: window
# % type: integer
# % label: Analysis window size in pixels (odd number)
# % description: Should approximate the landing ellipse minor axis at DEM resolution.
# % answer: 11
# % required: no
# %end

# %option
# % key: threshold
# % type: double
# % label: RMS roughness threshold in metres for exclusion mask
# % answer: 0.5
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: roughness
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
from p_lib import cleanup_prefix

_PREFIX_TMP = "pterr_rough_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def main():
    opt_dem   = options["dem"]
    opt_slope = options["slope"]
    opt_win   = int(options["window"])
    opt_thr   = float(options["threshold"])
    opt_pfx   = options["prefix"]
    opt_nprocs = int(options.get("nprocs", 1) or 1)
    opt_memory = int(options.get("memory", 300) or 300)
    _PAR = {"nprocs": opt_nprocs, "memory": opt_memory}

    pid = os.getpid()

    if opt_win % 2 == 0:
        opt_win += 1
        gs.warning(f"Window size adjusted to odd number: {opt_win}")

    # ── 1. Slope (compute if not provided) ───────────────────────────────
    if opt_slope:
        slope = opt_slope
        own_slope = False
    else:
        slope = f"{_PREFIX_TMP}slope_{pid}"
        gs.message("Computing slope…")
        gs.run_command("r.slope.aspect",
                       elevation=opt_dem,
                       slope=slope,
                       format="degrees",
                       quiet=True,
                       overwrite=True, **_PAR)
        own_slope = True

    # ── 2. RMS roughness ─────────────────────────────────────────────────
    # Local mean elevation in window
    dem_mean = f"{_PREFIX_TMP}dem_mean_{pid}"
    gs.run_command("r.neighbors",
                   input=opt_dem,
                   output=dem_mean,
                   method="average",
                   size=opt_win,
                   quiet=True,
                   overwrite=True, **_PAR)

    # Squared deviation from local mean
    dem_sq_dev = f"{_PREFIX_TMP}dem_sqdev_{pid}"
    gs.mapcalc(f"{dem_sq_dev} = ({opt_dem} - {dem_mean})^2",
               overwrite=True, quiet=True)

    # Mean of squared deviations in window = variance
    dem_var = f"{_PREFIX_TMP}dem_var_{pid}"
    gs.run_command("r.neighbors",
                   input=dem_sq_dev,
                   output=dem_var,
                   method="average",
                   size=opt_win,
                   quiet=True,
                   overwrite=True, **_PAR)

    # RMS = sqrt(variance)
    rms_out = f"{opt_pfx}_rms"
    gs.mapcalc(f"{rms_out} = sqrt({dem_var})",
               overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=rms_out,
                   title="RMS terrain roughness",
                   units="meters",
                   source1="p.terrain.roughness", quiet=True)
    gs.run_command("r.colors", map=rms_out, color="plasma", quiet=True)

    # ── 3. Coefficient of variation of slope ─────────────────────────────
    slope_mean = f"{_PREFIX_TMP}slope_mean_{pid}"
    gs.run_command("r.neighbors",
                   input=slope,
                   output=slope_mean,
                   method="average",
                   size=opt_win,
                   quiet=True,
                   overwrite=True, **_PAR)

    slope_stddev = f"{_PREFIX_TMP}slope_std_{pid}"
    gs.run_command("r.neighbors",
                   input=slope,
                   output=slope_stddev,
                   method="stddev",
                   size=opt_win,
                   quiet=True,
                   overwrite=True, **_PAR)

    cv_out = f"{opt_pfx}_cv"
    # CV = stddev / mean; protect against division by zero
    gs.mapcalc(
        f"{cv_out} = if({slope_mean} > 0.001, {slope_stddev} / {slope_mean}, 0.0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=cv_out,
                   title="Coefficient of variation of slope",
                   units="dimensionless",
                   source1="p.terrain.roughness", quiet=True)
    gs.run_command("r.colors", map=cv_out, color="plasma", quiet=True)

    # ── 4. Local Moran's I proxy ─────────────────────────────────────────
    # Deviation of slope from local mean
    slope_dev = f"{_PREFIX_TMP}slope_dev_{pid}"
    gs.mapcalc(f"{slope_dev} = {slope} - {slope_mean}",
               overwrite=True, quiet=True)

    # Spatial lag: mean of neighbour deviations (queen adjacency via r.neighbors)
    slope_lag = f"{_PREFIX_TMP}slope_lag_{pid}"
    gs.run_command("r.neighbors",
                   input=slope_dev,
                   output=slope_lag,
                   method="average",
                   size=3,
                   quiet=True,
                   overwrite=True, **_PAR)

    # Cross-product: dev × lag_mean (numerator contribution per pixel)
    xprod = f"{_PREFIX_TMP}xprod_{pid}"
    gs.mapcalc(f"{xprod} = {slope_dev} * {slope_lag}",
               overwrite=True, quiet=True)

    # Window sums of cross-product and squared deviations
    xprod_sum = f"{_PREFIX_TMP}xprod_sum_{pid}"
    sqdev_sum  = f"{_PREFIX_TMP}sqdev_sum_{pid}"
    sq_dev_map = f"{_PREFIX_TMP}sq_dev_{pid}"
    gs.mapcalc(f"{sq_dev_map} = {slope_dev}^2",
               overwrite=True, quiet=True)

    gs.run_command("r.neighbors", input=xprod,     output=xprod_sum,
                   method="sum", size=opt_win, quiet=True, overwrite=True, **_PAR)
    gs.run_command("r.neighbors", input=sq_dev_map, output=sqdev_sum,
                   method="sum", size=opt_win, quiet=True, overwrite=True, **_PAR)

    # Moran's I = sum(dev_i * lag_i) / sum(dev_i^2)
    morans_out = f"{opt_pfx}_moransI"
    gs.mapcalc(
        f"{morans_out} = if({sqdev_sum} > 0.0001, {xprod_sum} / {sqdev_sum}, 0.0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=morans_out,
                   title="Local Moran's I (slope spatial autocorrelation)",
                   units="dimensionless",
                   source1="p.terrain.roughness", quiet=True)
    gs.run_command("r.colors", map=morans_out, color="differences", quiet=True)

    # ── 5. Exclusion mask ────────────────────────────────────────────────
    mask_out = f"{opt_pfx}_mask"
    gs.mapcalc(
        f"{mask_out} = if({rms_out} > {opt_thr}, 1, 0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=mask_out,
                   title=f"Roughness exclusion mask (RMS > {opt_thr} m)",
                   source1="p.terrain.roughness", quiet=True)

    # ── clean temporaries ────────────────────────────────────────────────
    tmps = [dem_mean, dem_sq_dev, dem_var, slope_mean, slope_stddev,
            slope_dev, slope_lag, xprod, xprod_sum, sqdev_sum, sq_dev_map]
    if own_slope:
        tmps.append(slope)
    gs.run_command("g.remove", type="raster",
                   name=",".join(tmps), flags="f", quiet=True)

    gs.message("Output maps:")
    for m in [rms_out, cv_out, morans_out, mask_out]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
