#!/usr/bin/env python3
############################################################################
# MODULE:       p.terrain.slope
# PURPOSE:      Compute slope at multiple length scales relevant to landing.
#               Each scale produces a slope raster and a binary exclusion mask.
#               A composite worst-case mask covers all scales.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Multi-scale slope hazard maps for planetary landing.
# % keyword: Planetary
# % keyword: Terrain Analysis
# % keyword: slope
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option
# % key: scales
# % type: string
# % label: Comma-separated analysis scales in metres
# % description: DEM is resampled to each scale before slope computation.
# % answer: 30,100,1000,10000
# % required: no
# %end

# %option
# % key: thresholds
# % type: string
# % label: Comma-separated slope thresholds in degrees (one per scale)
# % description: Pixels exceeding the threshold at each scale are excluded.
# % answer: 15,10,7,5
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: slope
# % required: no
# %end

# %option
# % key: nprocs
# % type: integer
# % label: OpenMP threads for r.slope.aspect
# % answer: 1
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % label: r.slope.aspect row-cache size in MB
# % answer: 300
# % required: no
# %end

# %flag
# % key: k
# % description: Keep per-scale resampled DEMs
# %end

import os
import sys
import atexit

import grass.script as gs
from grass.exceptions import CalledModuleError

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import cleanup_prefix

_PREFIX_TMP = "pterr_slope_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def slope_at_scale(dem, scale_m, threshold_deg, prefix, keep_dem,
                   nprocs=1, memory=300):
    """
    Resample DEM to scale_m resolution, compute slope, create exclusion mask.
    Returns (slope_map, mask_map).
    """
    pid = os.getpid()
    scale_tag = f"{int(scale_m)}m"
    dem_s   = f"{_PREFIX_TMP}dem_{scale_tag}_{pid}"
    slope_o = f"{prefix}_{scale_tag}"
    mask_o  = f"{prefix}_mask_{scale_tag}"

    # Clamp the requested scale to the DEM's native posting. Asking for a
    # 5 m slope on a 30 m DEM would make r.resamp.stats average sub-cells
    # of a single parent — producing an all-NULL raster, which then breaks
    # downstream MCDM normalisation. Producing the layer at the native
    # posting instead keeps the API stable (slope_5m exists, with content
    # equivalent to slope_<native>m) and lets coarse-DEM sites pass MCDM.
    info = gs.raster_info(dem)
    native_res = max(float(info["nsres"]), float(info["ewres"]))
    effective_scale = max(float(scale_m), native_res)
    if effective_scale > float(scale_m):
        gs.warning(
            f"Requested {int(scale_m)} m slope is finer than DEM posting "
            f"({native_res:g} m); computing at native posting instead.")

    # Save current region, set to DEM then change resolution
    gs.use_temp_region()
    gs.run_command("g.region", raster=dem, res=effective_scale,
                   flags="a", quiet=True)

    gs.run_command(
        "r.resamp.stats",
        input=dem,
        output=dem_s,
        method="average",
        quiet=True,
        overwrite=True,
    )

    gs.run_command(
        "r.slope.aspect",
        elevation=dem_s,
        slope=slope_o,
        format="degrees",
        nprocs=int(nprocs),
        memory=int(memory),
        quiet=True,
        overwrite=gs.overwrite(),
    )

    gs.mapcalc(
        f"{mask_o} = if({slope_o} > {threshold_deg}, 1, 0)",
        overwrite=gs.overwrite(),
        quiet=True,
    )

    gs.run_command("r.colors", map=slope_o, color="slope", quiet=True)
    gs.run_command("r.support",
                   map=slope_o,
                   title=f"Slope at {scale_tag}",
                   units="degrees",
                   source1="p.terrain.slope",
                   quiet=True)
    gs.run_command("r.support",
                   map=mask_o,
                   title=f"Slope exclusion mask at {scale_tag} (thr={threshold_deg}°)",
                   source1="p.terrain.slope",
                   quiet=True)

    gs.del_temp_region()

    if not keep_dem:
        gs.run_command("g.remove", type="raster",
                       name=dem_s, flags="f", quiet=True)

    return slope_o, mask_o


def main():
    opt_dem       = options["dem"]
    opt_scales    = options["scales"]
    opt_thresholds= options["thresholds"]
    opt_prefix    = options["prefix"]
    opt_nprocs    = int(options.get("nprocs", 1) or 1)
    opt_memory    = int(options.get("memory", 300) or 300)
    flag_keep     = flags["k"]

    scales     = [float(s.strip()) for s in opt_scales.split(",")]
    thresholds = [float(t.strip()) for t in opt_thresholds.split(",")]

    if len(thresholds) != len(scales):
        gs.fatal(
            f"Number of thresholds ({len(thresholds)}) must match "
            f"number of scales ({len(scales)})."
        )

    slope_maps = []
    mask_maps  = []

    for scale, thr in zip(scales, thresholds):
        gs.message(f"Computing slope at {scale:.0f} m scale (threshold {thr}°)…")
        sm, mm = slope_at_scale(opt_dem, scale, thr, opt_prefix, flag_keep,
                                nprocs=opt_nprocs, memory=opt_memory)
        slope_maps.append(sm)
        mask_maps.append(mm)

    # ── composite worst-case mask (excluded if flagged at ANY scale) ──────
    composite_mask = f"{opt_prefix}_safe_mask"
    # safe_mask = 1 where safe at ALL scales, 0 where excluded at any scale
    union_expr = " + ".join(mask_maps)
    gs.mapcalc(
        f"{composite_mask} = if(({union_expr}) > 0, 0, 1)",
        overwrite=gs.overwrite(),
        quiet=True,
    )
    gs.run_command("r.support",
                   map=composite_mask,
                   title="Multi-scale slope safe mask (1=safe, 0=excluded)",
                   source1="p.terrain.slope",
                   quiet=True)
    gs.write_command("r.colors",
                    map=composite_mask,
                    rules="-",
                    quiet=True,
                    stdin="0 red\n1 green\n")

    gs.message("Output maps:")
    for m in slope_maps + mask_maps + [composite_mask]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
