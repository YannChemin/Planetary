#!/usr/bin/env python3
############################################################################
# MODULE:       p.visibility.earth
# PURPOSE:      Fraction of time each surface pixel has direct line-of-sight
#               to Earth (or a relay body) over a full nutation cycle.
#               Uses pre-computed r.horizon outputs to check horizon masking
#               at each Earth azimuth/elevation step; no duplicate logic.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Earth/relay visibility fraction over a full planetary cycle.
# % keyword: Planetary
# % keyword: Visibility
# % keyword: communication
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: body
# % label: Body descriptor JSON file
# % required: yes
# %end

# %option
# % key: nsteps
# % type: integer
# % label: Number of time steps to simulate
# % answer: 36
# % required: no
# %end

# %option
# % key: window_days
# % type: double
# % label: Sample only over a window of this many days starting at start_epoch
# % description: 0 (default) means sample across the body's full nutation cycle, the conventional long-term Earth-visibility integration. Set to a positive value to model a short mission window (e.g. 6.5 for an Artemis-style 6.5-day surface stay); the `nsteps` samples are then spread evenly across that window only.
# % answer: 0
# % required: no
# %end

# %option
# % key: min_elevation
# % type: double
# % label: Minimum Earth elevation angle above local horizon (degrees)
# % answer: 3.0
# % required: no
# %end

# %option
# % key: horizon_step
# % type: double
# % label: Angular step for pre-computing horizon angles (degrees)
# % answer: 22.5
# % required: no
# %end

# %option
# % key: scan_res
# % type: double
# % label: Resolution for horizon computation in metres (0 = native DEM resolution)
# % description: Coarsen to e.g. 30 or 100 m to speed up r.horizon and match published methodology resolutions.
# % answer: 0
# % required: no
# %end

# %option
# % key: ephemeris
# % type: string
# % label: Sub-Earth point model
# % description: auto = SPICE if configured, else Meeus libration (Moon), else analytic; spice = force SPICE (needs p.spice.config); meeus = force Meeus (Moon only); analytic = force toy model.
# % options: auto,spice,meeus,analytic
# % answer: auto
# % required: no
# %end

# %option
# % key: start_epoch
# % type: string
# % label: UTC start epoch for the real ephemeris (ISO-8601, e.g. 2028-01-01T00:00:00)
# % description: Only used when ephemeris=auto/meeus. Defaults to body JSON 'start_epoch' or J2000.
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: earth_vis
# % required: no
# %end

# %option
# % key: nprocs
# % type: integer
# % label: OpenMP threads for r.horizon (parallelises the azimuth precompute)
# % answer: 1
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % label: r.horizon row-cache size in MB
# % answer: 300
# % required: no
# %end

import os
import sys
import math
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import (body_params, cleanup_prefix,
                   sun_position_moon, sun_position_generic,
                   solar_elevation_azimuth, region_center_geographic,
                   precompute_horizons, interpolate_horizon,
                   subearth_point_moon, iso_to_jd)
import p_spice

_PREFIX_TMP = "pvis_earth_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)




def earth_position_moon(t_days, body):
    """
    Approximate sub-Earth point on the Moon (libration simplified).
    Earth is essentially fixed at 0° lat, 0° lon (mean Earth reference),
    with libration ±8° in lon, ±7° in lat over the nutation period.
    """
    nut_days = body.get("nutation_period_years", 18.6) * 365.25
    syn_days = body.get("synodic_period_days", 29.53)
    # Longitude libration (physical + optical)
    lon = 8.0 * math.sin(2 * math.pi * t_days / syn_days)
    # Latitude libration
    lat = 7.0 * math.sin(2 * math.pi * t_days / nut_days)
    return lat, lon


def earth_position_generic(t_days, body):
    """Earth fixed at lon=0, lat=0 for non-Moon bodies (relay body at sub-point)."""
    return 0.0, 0.0


def main():
    opt_dem       = options["dem"]
    opt_body      = options["body"]
    opt_nsteps    = int(options["nsteps"])
    opt_min_el    = float(options["min_elevation"])
    opt_hor_step  = float(options["horizon_step"])
    opt_scan_res  = float(options.get("scan_res", 0) or 0)
    opt_ephem     = (options.get("ephemeris") or "auto").lower()
    opt_start_ep  = options.get("start_epoch") or ""
    opt_pfx       = options["prefix"]
    opt_nprocs    = int(options.get("nprocs", 1) or 1)
    opt_memory    = int(options.get("memory", 300) or 300)

    body = body_params(opt_body)
    pid  = os.getpid()
    body_name = body.get("name", "").lower()

    # Sub-Earth point model. Cascade for ephemeris=auto:
    #   SPICE sub-observer point (if a mapset meta-kernel is configured)
    #   → Meeus libration (Earth's Moon only)
    #   → analytic toy model (any body).
    epoch = opt_start_ep or body.get("start_epoch") or "2000-01-01T12:00:00"
    use_spice = use_meeus = False
    jd_start = et_start = None
    spice_target = spice_frame = spice_obs = None

    if opt_ephem in ("auto", "spice"):
        cfg = None
        if p_spice.spice_available():
            try:
                cfg = p_spice.activate_from_mapset()
            except Exception as e:
                gs.warning(f"SPICE activation failed: {e}")
        if cfg:
            spice_target = cfg.get("P_SPICE_TARGET") or "MOON"
            spice_frame  = cfg.get("P_SPICE_FRAME") or "IAU_MOON"
            spice_obs    = cfg.get("P_SPICE_OBSERVER") or "EARTH"
            try:
                et_start = p_spice.str2et(epoch)
                use_spice = True
                gs.message(f"Sub-{spice_obs} point: SPICE "
                           f"({spice_target}/{spice_frame}) from epoch {epoch}.")
            except p_spice.SpiceError as e:
                gs.warning(f"SPICE time conversion failed: {e}")
        if not use_spice and opt_ephem == "spice":
            gs.fatal("ephemeris=spice requested but no usable SPICE configuration. "
                     "Run p.in.spice then p.spice.config, and ensure libcspice "
                     "is installed (or set $CSPICE_LIB).")

    if not use_spice and opt_ephem in ("auto", "meeus") and body_name == "moon":
        jd_start = iso_to_jd(epoch)
        use_meeus = True
        gs.message(f"Sub-Earth point: real Meeus libration from epoch {epoch} "
                   f"(JD {jd_start:.1f}).")
    elif not use_spice and opt_ephem == "meeus":
        gs.warning(f"ephemeris=meeus requested but body '{body_name}' has no "
                   "real ephemeris; falling back to the analytic model.")

    if not use_spice and not use_meeus:
        earth_pos_fn = earth_position_moon if body_name == "moon" else earth_position_generic
        gs.message("Sub-Earth point: analytic (approximate) model.")

    # Total simulation period: explicit window_days wins, else fall back to
    # the body's nutation cycle (or sidereal period if no nutation).
    opt_window = float(options.get("window_days", 0) or 0)
    if opt_window > 0:
        total_days = opt_window
        gs.message(f"Mission window: {total_days:.2f} days starting "
                   f"{opt_start_ep or 'epoch 0'}.")
    elif body.get("nutation_period_years", 0) > 0:
        total_days = body["nutation_period_years"] * 365.25
    else:
        total_days = body.get("sidereal_period_days", 365.25)

    dt = total_days / opt_nsteps

    # ── optional DEM resampling for faster horizon computation ────────────
    if opt_scan_res > 0:
        gs.use_temp_region()
        # Do NOT pass raster=opt_dem: if the in-mapset DEM is the full
        # polar cap (e.g. ldem_85s_20m), that would expand the region
        # from the caller's per-region box to the entire cap before
        # applying res=opt_scan_res, blowing the cell count by ~400× and
        # making r.horizon timeout. The caller already set the region.
        gs.run_command("g.region", res=opt_scan_res, flags="a", quiet=True)
        dem_hor = f"{_PREFIX_TMP}dem_scan_{pid}"
        gs.run_command("r.resamp.stats", input=opt_dem, output=dem_hor,
                       method="average", quiet=True, overwrite=True)
    else:
        dem_hor = opt_dem

    # ── pre-compute horizon maps ─────────────────────────────────────────
    gs.message(f"Pre-computing horizon angles (step={opt_hor_step}°)…")
    hor_base = f"{_PREFIX_TMP}hor_{pid}"
    horizons = precompute_horizons(dem_hor, hor_base, opt_hor_step, body=body,
                                   nprocs=opt_nprocs, memory=opt_memory)

    if opt_scan_res > 0:
        gs.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       name=dem_hor, flags="f", quiet=True)

    # ── visibility accumulator ────────────────────────────────────────────
    # Vectorised: gather per-step contributions in pure Python, then run a
    # SINGLE mapcalc that sums all per-step if(...) terms. Each horizon
    # raster is read exactly once. Collapses the per-step 3-mapcalc pattern
    # into one expression (see project_orbiter_loop_vectorize memory).
    accum = f"{_PREFIX_TMP}accum_{pid}"
    center_lat, center_lon = region_center_geographic()
    hor_fallback = next(iter(horizons.values())) if horizons else None

    gs.message(f"Simulating {opt_nsteps} time steps…")

    terms = []
    n_steps_used = 0
    for i in range(opt_nsteps):
        t = i * dt
        if use_spice:
            earth_lat, earth_lon = p_spice.subobserver_point(
                spice_target, spice_frame, et_start + t * 86400.0, spice_obs)
        elif use_meeus:
            earth_lat, earth_lon = subearth_point_moon(jd_start + t)
        else:
            earth_lat, earth_lon = earth_pos_fn(t, body)

        earth_elev, earth_az = solar_elevation_azimuth(
            center_lat, center_lon, earth_lat, earth_lon)
        if earth_elev < -10.0:
            continue

        # Mirror interpolate_horizon: compass(CW-from-N) → CCW-from-east,
        # then linear interpolation between two bracketing horizon maps,
        # inlined into the comparison.
        az_ccw = (90.0 - earth_az) % 360.0
        az_lo  = (int(az_ccw / opt_hor_step) * opt_hor_step) % 360.0
        az_hi  = (az_lo + opt_hor_step) % 360.0
        w      = (az_ccw - az_lo) / opt_hor_step
        m_lo   = horizons.get(az_lo, hor_fallback)
        m_hi   = horizons.get(az_hi, hor_fallback)

        terms.append(
            f"if({earth_elev:.6f} > (1.0-{w:.6f})*{m_lo} + "
            f"{w:.6f}*{m_hi} + {opt_min_el:.6f}, 1, 0)")
        n_steps_used += 1

    gs.message(f"Steps contributing: {n_steps_used}/{opt_nsteps}")

    if n_steps_used == 0:
        gs.mapcalc(f"{accum} = 0", overwrite=True, quiet=True)
    else:
        gs.mapcalc(f"{accum} = " + " + ".join(terms),
                   overwrite=True, quiet=True)

    # ── fraction ─────────────────────────────────────────────────────────
    frac_out = f"{opt_pfx}_fraction"
    if n_steps_used > 0:
        gs.mapcalc(
            f"{frac_out} = double({accum}) / {n_steps_used}.0",
            overwrite=gs.overwrite(), quiet=True)
    else:
        gs.mapcalc(f"{frac_out} = 0.0", overwrite=gs.overwrite(), quiet=True)

    gs.run_command("r.support", map=frac_out,
                   title="Earth/relay visibility fraction [0=never, 1=always]",
                   units="fraction",
                   source1="p.visibility.earth", quiet=True)
    gs.run_command("r.colors", map=frac_out, color="bcyr", quiet=True)

    # ── binary mask above threshold ───────────────────────────────────────
    mask_out = f"{opt_pfx}_mask"
    threshold = body.get("earth_visibility_min_fraction", 0.5)
    gs.mapcalc(
        f"{mask_out} = if({frac_out} >= {threshold}, 1, 0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=mask_out,
                   title=f"Earth visibility mask (fraction >= {threshold})",
                   source1="p.visibility.earth", quiet=True)

    # ── clean up ─────────────────────────────────────────────────────────
    del_maps = list(horizons.values()) + [accum]
    gs.run_command("g.remove", type="raster",
                   name=",".join(del_maps), flags="f", quiet=True)

    gs.message("Output maps:")
    for m in [frac_out, mask_out]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
