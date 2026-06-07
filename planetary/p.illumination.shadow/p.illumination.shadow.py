#!/usr/bin/env python3
############################################################################
# MODULE:       p.illumination.shadow
# PURPOSE:      Shadow frequency, solar incidence angle statistics, and
#               extreme lighting condition masks over a planetary cycle.
#               Complements p.illumination.sunfraction; does not duplicate it.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Shadow frequency and extreme lighting masks for any planetary body.
# % keyword: Planetary
# % keyword: Illumination
# % keyword: shadow
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
# % key: shadow_threshold
# % type: double
# % label: Shadow frequency above which a pixel is flagged as hazardous (0-1)
# % answer: 0.70
# % required: no
# %end

# %option
# % key: grazing_threshold
# % type: double
# % label: Solar elevation below which lighting is considered extreme grazing (degrees)
# % answer: 5.0
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: shadow
# % required: no
# %end

# %option
# % key: sunmask_module
# % type: string
# % label: Shadow-mask module to call (r.sunmask or p.sunmask)
# % description: Use p.sunmask for OpenMP+OpenCL acceleration (recommended).
# % answer: p.sunmask
# % required: no
# %end

# %option
# % key: ephemeris
# % type: string
# % label: Sub-solar point model
# % description: auto = SPICE if configured, else Meeus (Moon), else analytic; spice = force SPICE (needs p.spice.config); meeus = force Meeus (Moon only); analytic = force toy model.
# % options: auto,spice,meeus,analytic
# % answer: auto
# % required: no
# %end

# %option
# % key: start_epoch
# % type: string
# % label: UTC start epoch for the real ephemeris (ISO-8601, e.g. 2028-01-01T00:00:00)
# % description: Only used when ephemeris=auto/meeus/spice. Defaults to body JSON 'start_epoch' or J2000.
# % required: no
# %end

import os
import sys
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import (body_params, cleanup_prefix, region_center_geographic,
                   sun_position_moon, sun_position_generic,
                   solar_elevation_azimuth, subsolar_point_moon, iso_to_jd)
import p_spice

_PREFIX_TMP = "pillum_shadow_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def main():
    opt_dem        = options["dem"]
    opt_body       = options["body"]
    opt_nsteps     = int(options["nsteps"])
    opt_shad_thr   = float(options["shadow_threshold"])
    opt_graze_thr  = float(options["grazing_threshold"])
    opt_pfx        = options["prefix"]
    opt_sunmask    = options["sunmask_module"]
    opt_ephem      = (options.get("ephemeris") or "auto").lower()
    opt_start_ep   = options.get("start_epoch") or ""

    body = body_params(opt_body)
    pid  = os.getpid()

    # Sub-solar point model. Cascade for ephemeris=auto:
    #   SPICE (if a mapset meta-kernel is configured and libcspice loads)
    #   → Meeus real ephemeris (Earth's Moon only)
    #   → analytic single-sine toy model (any body).
    body_name = body.get("name", "").lower()
    epoch = opt_start_ep or body.get("start_epoch") or "2000-01-01T12:00:00"
    use_spice = use_meeus = False
    jd_start = et_start = None
    spice_target = spice_frame = None
    sun_pos_fn = None

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
            try:
                et_start = p_spice.str2et(epoch)
                use_spice = True
                gs.message(f"Sub-solar point: SPICE ({spice_target}/{spice_frame}) "
                           f"from epoch {epoch}.")
            except p_spice.SpiceError as e:
                gs.warning(f"SPICE time conversion failed: {e}")
        if not use_spice and opt_ephem == "spice":
            gs.fatal("ephemeris=spice requested but no usable SPICE configuration. "
                     "Run p.in.spice then p.spice.config, and ensure libcspice "
                     "is installed (or set $CSPICE_LIB).")

    if not use_spice and opt_ephem in ("auto", "meeus") and body_name == "moon":
        jd_start = iso_to_jd(epoch)
        use_meeus = True
        gs.message(f"Sub-solar point: real Meeus ephemeris from epoch {epoch} "
                   f"(JD {jd_start:.1f}).")
    elif not use_spice and opt_ephem == "meeus":
        gs.warning(f"ephemeris=meeus requested but body '{body_name}' has no "
                   "real ephemeris; falling back to the analytic model.")

    if not use_spice and not use_meeus:
        sun_pos_fn = sun_position_moon if body_name == "moon" else sun_position_generic
        gs.message("Sub-solar point: analytic (approximate) model.")

    if body.get("nutation_period_years", 0) > 0:
        total_days = body["nutation_period_years"] * 365.25
    else:
        total_days = body.get("sidereal_period_days", 365.25)

    dt = total_days / opt_nsteps

    center_lat, center_lon = region_center_geographic()

    shadow_maps  = []   # 1=shadow, 0=lit (inverted sunmask)
    incid_maps   = []   # per-step solar elevation (scalar applied as map)
    graze_maps   = []   # 1=grazing light step
    n_used = 0

    gs.message(f"Simulating {opt_nsteps} steps for shadow/incidence…")

    for i in range(opt_nsteps):
        t = i * dt
        if use_spice:
            sub_lat, sub_lon = p_spice.subsolar_point(
                spice_target, spice_frame, et_start + t * 86400.0)
        elif use_meeus:
            sub_lat, sub_lon = subsolar_point_moon(jd_start + t)
        else:
            sub_lat, sub_lon = sun_pos_fn(t, body)
        elev, azim = solar_elevation_azimuth(
            center_lat, center_lon, sub_lat, sub_lon)

        if elev <= 0.0:
            continue

        n_used += 1
        sun_name  = f"{_PREFIX_TMP}sun_{i:04d}_{pid}"
        shad_name = f"{_PREFIX_TMP}shad_{i:04d}_{pid}"

        # Call shadow module (r.sunmask or p.sunmask): 1=sunlit, 0/null=shadow
        gs.run_command(opt_sunmask, elevation=opt_dem,
                       output=sun_name, azimuth=azim, altitude=min(elev, 89.999),
                       quiet=True, overwrite=True)
        gs.run_command("r.null", map=sun_name, null=0, quiet=True)

        # Invert: shadow=1, sunlit=0
        gs.mapcalc(f"{shad_name} = 1 - {sun_name}",
                   overwrite=True, quiet=True)
        shadow_maps.append(shad_name)

        # Grazing light: any lit pixel at this step where elev < grazing_threshold
        if elev < opt_graze_thr:
            grz = f"{_PREFIX_TMP}graze_{i:04d}_{pid}"
            gs.mapcalc(f"{grz} = {sun_name}",   # 1=lit AND grazing
                       overwrite=True, quiet=True)
            graze_maps.append(grz)

        gs.run_command("g.remove", type="raster",
                       name=sun_name, flags="f", quiet=True)

    gs.message(f"Steps used: {n_used}/{opt_nsteps}")

    if not shadow_maps:
        gs.fatal("Sun never above horizon. Check body parameters.")

    # ── shadow frequency ─────────────────────────────────────────────────
    freq_out = f"{opt_pfx}_frequency"
    gs.run_command("r.series", input=",".join(shadow_maps),
                   output=freq_out, method="average",
                   quiet=True, overwrite=gs.overwrite())
    gs.run_command("r.support", map=freq_out,
                   title="Shadow frequency [0=never shadowed, 1=always shadowed]",
                   units="fraction", source1="p.illumination.shadow", quiet=True)
    gs.run_command("r.colors", map=freq_out, color="blues", quiet=True)

    # ── shadow hazard mask ────────────────────────────────────────────────
    mask_out = f"{opt_pfx}_mask"
    gs.mapcalc(
        f"{mask_out} = if({freq_out} >= {opt_shad_thr}, 1, 0)",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=mask_out,
                   title=f"Shadow hazard mask (frequency >= {opt_shad_thr})",
                   source1="p.illumination.shadow", quiet=True)

    # ── temporal shadow variability ───────────────────────────────────────
    var_out = f"{opt_pfx}_variability"
    gs.run_command("r.series", input=",".join(shadow_maps),
                   output=var_out, method="stddev",
                   quiet=True, overwrite=gs.overwrite())
    gs.run_command("r.support", map=var_out,
                   title="Shadow temporal variability (stddev over time steps)",
                   source1="p.illumination.shadow", quiet=True)

    # ── extreme grazing light mask ────────────────────────────────────────
    graze_out = f"{opt_pfx}_extreme_incidence"
    if graze_maps:
        gs.run_command("r.series", input=",".join(graze_maps),
                       output=graze_out, method="sum",
                       quiet=True, overwrite=gs.overwrite())
        gs.mapcalc(f"{graze_out} = if({graze_out} > 0, 1, 0)",
                   overwrite=True, quiet=True)
    else:
        gs.mapcalc(f"{graze_out} = 0", overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=graze_out,
                   title=f"Extreme grazing light mask (sun elev < {opt_graze_thr}°)",
                   source1="p.illumination.shadow", quiet=True)

    # ── clean temporary maps ──────────────────────────────────────────────
    all_tmp = shadow_maps + graze_maps
    if all_tmp:
        gs.run_command("g.remove", type="raster",
                       name=",".join(all_tmp), flags="f", quiet=True)

    gs.message("Output maps:")
    for m in [freq_out, mask_out, var_out, graze_out]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
