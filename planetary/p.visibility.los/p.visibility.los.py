#!/usr/bin/env python3
############################################################################
# MODULE:       p.visibility.los
# PURPOSE:      Horizon masking angle map (maximum terrain horizon in any
#               direction) and line-of-sight maps to user-specified
#               base/relay stations.  Uses r.horizon and r.viewshed.
#               Does not duplicate either module.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Horizon masking angle map and LOS to base/relay stations.
# % keyword: Planetary
# % keyword: Visibility
# % keyword: horizon
# % keyword: LOS
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: body
# % label: Body descriptor JSON file (used for curvature correction)
# % required: no
# %end

# %option
# % key: directions
# % type: integer
# % label: Number of horizon directions (evenly spaced 0-360°)
# % answer: 16
# % required: no
# %end

# %option
# % key: sites
# % type: string
# % label: Base/relay site coordinates as lon,lat pairs separated by semicolons
# % description: e.g. "0.0,-89.5;15.0,-81.0" for two sites. Leave empty to skip viewshed.
# % required: no
# %end

# %option
# % key: observer_elev
# % type: double
# % label: Observer (lander/rover) height above ground in metres
# % answer: 2.0
# % required: no
# %end

# %option
# % key: target_elev
# % type: double
# % label: Target (relay antenna) height above ground in metres
# % answer: 10.0
# % required: no
# %end

# %option
# % key: max_distance
# % type: double
# % label: Maximum LOS search distance in metres (0 = unlimited)
# % answer: 0
# % required: no
# %end

# %option
# % key: scan_res
# % type: double
# % label: Resolution for horizon computation in metres (0 = native DEM resolution)
# % description: Coarsen to e.g. 30 or 100 m to speed up r.horizon on large DEMs.
# % answer: 0
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: los
# % required: no
# %end

import os
import sys
import math
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import body_params, cleanup_prefix, precompute_horizons

_PREFIX_TMP = "pvis_los_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def main():
    opt_dem       = options["dem"]
    opt_body      = options["body"]
    opt_ndirs     = int(options["directions"])
    opt_sites     = options["sites"]
    opt_obs_elev  = float(options["observer_elev"])
    opt_tgt_elev  = float(options["target_elev"])
    opt_maxdist   = float(options["max_distance"])
    opt_scan_res  = float(options["scan_res"])
    opt_pfx       = options["prefix"]

    pid = os.getpid()

    # Parse body descriptor for projection info
    body = body_params(opt_body)

    # ── 0. Optional: resample DEM for horizon computation ─────────────────
    # Horizon angles don't require native resolution; coarsening to 30-100 m
    # reduces r.horizon runtime by orders of magnitude on large DEMs.
    if opt_scan_res > 0:
        gs.message(f"Resampling DEM to {opt_scan_res:.0f} m for horizon computation…")
        gs.use_temp_region()
        reg = gs.region()
        # Do NOT pass raster=opt_dem: if the in-mapset DEM is the full
        # polar cap, that would expand the region from the caller's box
        # to the entire cap before applying res=opt_scan_res. The caller
        # already set the region; we only want to change the resolution.
        gs.run_command("g.region", res=opt_scan_res, flags="a", quiet=True)
        dem_hor = f"{_PREFIX_TMP}dem_scan_{pid}"
        gs.run_command("r.resamp.stats", input=opt_dem, output=dem_hor,
                       method="average", quiet=True, overwrite=True)
    else:
        dem_hor = opt_dem
        dem_scan_tmp = None

    # ── 1. Pre-compute horizon angles in N directions ─────────────────────
    step = 360.0 / opt_ndirs
    gs.message(f"Computing horizon in {opt_ndirs} directions (step={step:.1f}°)…")
    gs.message("  Note: 'stere: Invalid longitude' PROJ warnings are non-fatal "
               "in polar stereographic CRS.")

    hor_base = f"{_PREFIX_TMP}hor_{pid}"
    horizons = precompute_horizons(dem_hor, hor_base, step, body=body)
    hor_maps = list(horizons.values())

    if opt_scan_res > 0:
        gs.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       name=dem_hor, flags="f", quiet=True)

    # ── 2. Maximum horizon angle across all directions ────────────────────
    hor_max_out = f"{opt_pfx}_horizon_max"
    gs.run_command("r.series", input=",".join(hor_maps),
                   output=hor_max_out, method="maximum",
                   quiet=True, overwrite=gs.overwrite())
    gs.run_command("r.support", map=hor_max_out,
                   title="Maximum terrain horizon angle (degrees)",
                   units="degrees", source1="p.visibility.los", quiet=True)
    gs.run_command("r.colors", map=hor_max_out, color="plasma", quiet=True)

    # ── 3. LOS viewshed to each specified site ────────────────────────────
    site_maps = []
    if opt_sites.strip():
        gs.message("Computing LOS viewsheds to specified sites…")
        for i, site_str in enumerate(opt_sites.split(";")):
            site_str = site_str.strip()
            if not site_str:
                continue
            try:
                lon_s, lat_s = [float(x) for x in site_str.split(",")]
            except ValueError:
                gs.warning(f"Cannot parse site '{site_str}' — skipping.")
                continue

            # Convert geographic coordinates to map coordinates
            # (only works if region CRS uses lon/lat; for projected: user
            # must supply coordinates in projection units)
            coord_str = f"{lon_s},{lat_s}"

            vs_map = f"{opt_pfx}_viewshed_{i+1:02d}"
            vs_kwargs = dict(
                input=opt_dem,
                output=vs_map,
                coordinates=coord_str,
                observer_elevation=opt_obs_elev,
                target_elevation=opt_tgt_elev,
                quiet=True,
                overwrite=gs.overwrite(),
            )
            if opt_maxdist > 0:
                vs_kwargs["max_distance"] = opt_maxdist
            gs.run_command("r.viewshed", **vs_kwargs)
            gs.run_command("r.support", map=vs_map,
                           title=f"LOS viewshed to site {i+1} ({lon_s},{lat_s})",
                           source1="p.visibility.los", quiet=True)
            site_maps.append(vs_map)

        # ── 4. Combined LOS (any site visible) ───────────────────────────
        if len(site_maps) > 1:
            los_comb = f"{opt_pfx}_los_combined"
            expr = " + ".join(site_maps)
            gs.mapcalc(f"{los_comb} = if(({expr}) > 0, 1, 0)",
                       overwrite=gs.overwrite(), quiet=True)
            gs.run_command("r.support", map=los_comb,
                           title="Combined LOS (1=at least one site visible)",
                           source1="p.visibility.los", quiet=True)
            site_maps.append(los_comb)

    # ── clean horizon temporaries ─────────────────────────────────────────
    gs.run_command("g.remove", type="raster",
                   name=",".join(hor_maps), flags="f", quiet=True)

    gs.message("Output maps:")
    gs.message(f"  {hor_max_out}")
    for m in site_maps:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
