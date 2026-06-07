#!/usr/bin/env python3
############################################################################
# MODULE:       p.terrain.ellipse
# PURPOSE:      Sliding-window landing-ellipse suitability scan.
#               For each window of landing-ellipse dimensions, computes:
#               mean slope, threshold ratio (8° and 20°), coefficient of
#               variation, and Moran's I spatial autocorrelation.
#               Combines these with AHP weights (Liu et al. 2023, eq. 11)
#               into an overall rating Q.  Exports candidate ellipse windows
#               as a vector map ranked by Q.
#
#               Reference: Liu et al. 2023, Remote Sensing 15:3184.
#               https://doi.org/10.3390/rs15123184
#
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Sliding-window landing-ellipse suitability scan (slope metrics + AHP rating).
# % keyword: Planetary
# % keyword: Terrain Analysis
# % keyword: landing ellipse
# % keyword: AHP
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM (metres)
# % required: yes
# %end

# %option G_OPT_R_INPUT
# % key: slope
# % label: Pre-computed slope raster (degrees) — computed if omitted
# % required: no
# %end

# %option
# % key: ellipse_major
# % type: double
# % label: Landing ellipse major axis in metres
# % answer: 30000
# % required: no
# %end

# %option
# % key: ellipse_minor
# % type: double
# % label: Landing ellipse minor axis in metres
# % answer: 15000
# % required: no
# %end

# %option
# % key: scan_res
# % type: double
# % label: Working resolution for the window scan in metres
# % description: DEM is resampled to this resolution before the window scan. Choose so the minor axis spans 10-50 cells.
# % answer: 1000
# % required: no
# %end

# %option
# % key: slope_threshold_low
# % type: double
# % label: Low slope threshold for threshold ratio (degrees)
# % answer: 8.0
# % required: no
# %end

# %option
# % key: slope_threshold_high
# % type: double
# % label: High slope threshold for threshold ratio (degrees)
# % answer: 20.0
# % required: no
# %end

# %option
# % key: weights
# % type: string
# % label: AHP weights: mean,ratio_low,ratio_high,cv,moransI
# % description: From Liu et al. 2023 Table 2. Must sum to 1.
# % answer: 0.3632,0.3632,0.0767,0.1578,0.0391
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: ellipse
# % required: no
# %end

import os
import sys
import math
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import cleanup_prefix

_PREFIX_TMP = "pterr_ellipse_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def main():
    opt_dem       = options["dem"]
    opt_slope     = options["slope"]
    opt_major     = float(options["ellipse_major"])
    opt_minor     = float(options["ellipse_minor"])
    opt_scan_res  = float(options["scan_res"])
    opt_thr_lo    = float(options["slope_threshold_low"])
    opt_thr_hi    = float(options["slope_threshold_high"])
    opt_weights   = options["weights"]
    opt_pfx       = options["prefix"]

    w = [float(x.strip()) for x in opt_weights.split(",")]
    w_mean, w_rlo, w_rhi, w_cv, w_mi = (w + [0]*5)[:5]
    total_w = sum([w_mean, w_rlo, w_rhi, w_cv, w_mi])
    if abs(total_w - 1.0) > 0.01:
        gs.warning(f"Weights sum to {total_w:.4f}, not 1.0. Normalising.")
        w_mean /= total_w; w_rlo /= total_w; w_rhi /= total_w
        w_cv   /= total_w; w_mi  /= total_w

    pid = os.getpid()

    # ── 1. Compute window size in cells ─────────────────────────────────
    win_major = max(3, int(math.ceil(opt_major / opt_scan_res)))
    win_minor = max(3, int(math.ceil(opt_minor / opt_scan_res)))
    # r.neighbors requires odd size; use the smaller axis as the window
    win_cells = win_minor if win_minor % 2 == 1 else win_minor + 1
    gs.message(
        f"Scan resolution: {opt_scan_res:.0f} m  "
        f"→ window ≈ {win_major}×{win_minor} cells "
        f"(r.neighbors size={win_cells} cells)"
    )

    # ── 2. Resample DEM and compute slope at scan resolution ─────────────
    gs.use_temp_region()
    # Do NOT pass raster=opt_dem: if the in-mapset DEM is the full
    # polar cap, that would expand the region from the caller's box to
    # the entire cap before applying res=opt_scan_res. Caller's region
    # is the per-region landing box; we only change the resolution.
    gs.run_command("g.region", res=opt_scan_res, flags="a", quiet=True)

    dem_scan = f"{_PREFIX_TMP}dem_{pid}"
    gs.run_command("r.resamp.stats", input=opt_dem, output=dem_scan,
                   method="average", quiet=True, overwrite=True)

    if opt_slope:
        # Resample user-supplied slope to scan resolution
        slope_scan = f"{_PREFIX_TMP}slope_{pid}"
        gs.run_command("r.resamp.stats", input=opt_slope, output=slope_scan,
                       method="average", quiet=True, overwrite=True)
    else:
        slope_scan = f"{_PREFIX_TMP}slope_{pid}"
        gs.run_command("r.slope.aspect", elevation=dem_scan,
                       slope=slope_scan, format="degrees",
                       quiet=True, overwrite=True)

    # ── 3. Per-window slope statistics via r.neighbors ───────────────────

    # 3a. Mean slope in window
    slope_mean = f"{_PREFIX_TMP}smean_{pid}"
    gs.run_command("r.neighbors", input=slope_scan, output=slope_mean,
                   method="average", size=win_cells,
                   quiet=True, overwrite=True)

    # 3b. Threshold ratio (low) — fraction of pixels < thr_lo within window
    flat_lo = f"{_PREFIX_TMP}flat_lo_{pid}"
    gs.mapcalc(f"{flat_lo} = if({slope_scan} < {opt_thr_lo}, 1.0, 0.0)",
               overwrite=True, quiet=True)
    ratio_lo = f"{_PREFIX_TMP}ratio_lo_{pid}"
    gs.run_command("r.neighbors", input=flat_lo, output=ratio_lo,
                   method="average", size=win_cells,
                   quiet=True, overwrite=True)

    # 3c. Threshold ratio (high) — fraction of pixels < thr_hi
    flat_hi = f"{_PREFIX_TMP}flat_hi_{pid}"
    gs.mapcalc(f"{flat_hi} = if({slope_scan} < {opt_thr_hi}, 1.0, 0.0)",
               overwrite=True, quiet=True)
    ratio_hi = f"{_PREFIX_TMP}ratio_hi_{pid}"
    gs.run_command("r.neighbors", input=flat_hi, output=ratio_hi,
                   method="average", size=win_cells,
                   quiet=True, overwrite=True)

    # 3d. Coefficient of variation of slope within window
    slope_std = f"{_PREFIX_TMP}sstd_{pid}"
    gs.run_command("r.neighbors", input=slope_scan, output=slope_std,
                   method="stddev", size=win_cells,
                   quiet=True, overwrite=True)
    cv_map = f"{_PREFIX_TMP}cv_{pid}"
    gs.mapcalc(
        f"{cv_map} = if({slope_mean} > 0.001, {slope_std}/{slope_mean}, 0.0)",
        overwrite=True, quiet=True)

    # 3e. Moran's I proxy (same as p.terrain.roughness)
    slope_dev = f"{_PREFIX_TMP}sdev_{pid}"
    gs.mapcalc(f"{slope_dev} = {slope_scan} - {slope_mean}",
               overwrite=True, quiet=True)
    slope_lag = f"{_PREFIX_TMP}slag_{pid}"
    gs.run_command("r.neighbors", input=slope_dev, output=slope_lag,
                   method="average", size=3, quiet=True, overwrite=True)
    xprod    = f"{_PREFIX_TMP}xprod_{pid}"
    xprod_s  = f"{_PREFIX_TMP}xprods_{pid}"
    sqdev    = f"{_PREFIX_TMP}sqdev_{pid}"
    sqdev_s  = f"{_PREFIX_TMP}sqdevs_{pid}"
    gs.mapcalc(f"{xprod} = {slope_dev} * {slope_lag}",
               overwrite=True, quiet=True)
    gs.mapcalc(f"{sqdev} = {slope_dev}^2", overwrite=True, quiet=True)
    gs.run_command("r.neighbors", input=xprod, output=xprod_s,
                   method="sum", size=win_cells, quiet=True, overwrite=True)
    gs.run_command("r.neighbors", input=sqdev, output=sqdev_s,
                   method="sum", size=win_cells, quiet=True, overwrite=True)
    morans_i = f"{_PREFIX_TMP}mi_{pid}"
    gs.mapcalc(
        f"{morans_i} = if({sqdev_s} > 0.0001, {xprod_s}/{sqdev_s}, 0.0)",
        overwrite=True, quiet=True)

    # ── 4. AHP-weighted overall rating Q ─────────────────────────────────
    # Q = w_mean*(1-norm_mean) + w_rlo*ratio_lo + w_rhi*ratio_hi
    #   + w_cv*(1-norm_cv) + w_mi*morans_i
    # mean slope and CV are cost criteria (lower = better) → invert
    # Normalise mean slope and CV to [0,1] before inverting
    ms_stats = gs.parse_command("r.univar", map=slope_mean, flags="g", quiet=True)
    ms_min = float(ms_stats["min"]); ms_max = float(ms_stats["max"])
    cv_stats = gs.parse_command("r.univar", map=cv_map, flags="g", quiet=True)
    cv_min = float(cv_stats["min"]); cv_max = float(cv_stats["max"])

    rating_out = f"{opt_pfx}_rating"
    dms = ms_max - ms_min if ms_max > ms_min else 1.0
    dcv = cv_max - cv_min if cv_max > cv_min else 1.0

    gs.mapcalc(
        f"{rating_out} = "
        f"{w_mean} * (({ms_max} - {slope_mean}) / {dms}) + "
        f"{w_rlo}  * {ratio_lo} + "
        f"{w_rhi}  * {ratio_hi} + "
        f"{w_cv}   * (({cv_max} - {cv_map}) / {dcv}) + "
        f"{w_mi}   * if({morans_i} < 0, 0, {morans_i})",
        overwrite=gs.overwrite(), quiet=True)

    gs.run_command("r.colors", map=rating_out, color="plasma", quiet=True)
    gs.run_command("r.support", map=rating_out,
                   title=f"Ellipse suitability rating Q (Liu 2023, {opt_scan_res:.0f}m scan)",
                   units="dimensionless [0=poor, 1=best]",
                   source1="p.terrain.ellipse", quiet=True)

    # ── 5. Save per-metric output maps ────────────────────────────────────
    out_maps = {
        f"{opt_pfx}_slope_mean":    slope_mean,
        f"{opt_pfx}_ratio_lo":      ratio_lo,
        f"{opt_pfx}_ratio_hi":      ratio_hi,
        f"{opt_pfx}_cv":            cv_map,
        f"{opt_pfx}_moransI":       morans_i,
    }
    for dst, src in out_maps.items():
        gs.run_command("g.rename", raster=f"{src},{dst}",
                       quiet=True, overwrite=gs.overwrite())

    # ── 6. Vector candidate ellipses (top 20% windows as polygons) ────────
    cand_out = f"{opt_pfx}_candidates"
    stats = gs.parse_command("r.univar", map=rating_out, flags="g", quiet=True)
    thr80 = float(stats["min"]) + 0.80 * (float(stats["max"]) - float(stats["min"]))
    tmp_bin = f"{_PREFIX_TMP}topbin_{pid}"
    tmp_clump = f"{_PREFIX_TMP}topclump_{pid}"
    gs.mapcalc(f"{tmp_bin} = if({rating_out} >= {thr80}, 1, null())",
               overwrite=True, quiet=True)
    gs.run_command("r.clump", input=tmp_bin, output=tmp_clump,
                   quiet=True, overwrite=True)
    gs.run_command("r.to.vect", input=tmp_clump, output=cand_out,
                   type="area", quiet=True, overwrite=gs.overwrite())
    gs.run_command("v.db.addcolumn", map=cand_out,
                   columns="rating_mean DOUBLE PRECISION",
                   quiet=True)
    gs.run_command("v.rast.stats", map=cand_out, raster=rating_out,
                   column_prefix="q", method="average",
                   quiet=True)
    gs.run_command("g.remove", type="raster",
                   name=f"{tmp_bin},{tmp_clump}", flags="f", quiet=True)

    gs.del_temp_region()

    # clean remaining temporaries
    tmps = [dem_scan, flat_lo, flat_hi, slope_std, slope_dev, slope_lag,
            xprod, xprod_s, sqdev, sqdev_s]
    if not opt_slope:
        tmps.append(slope_scan)
    gs.run_command("g.remove", type="raster",
                   name=",".join(tmps), flags="f", quiet=True)

    gs.message("Output maps:")
    for m in list(out_maps.keys()) + [rating_out]:
        gs.message(f"  {m}")
    gs.message(f"  {cand_out} (vector — top 20% candidate ellipses)")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
