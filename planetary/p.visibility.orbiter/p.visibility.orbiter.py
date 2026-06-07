#!/usr/bin/env python3
############################################################################
# MODULE:       p.visibility.orbiter
# PURPOSE:      LOS contact fraction between surface pixels and an orbiting
#               relay asset over N simulated orbits.
#               Uses pre-computed r.horizon to check occlusion at each pass
#               azimuth/elevation, avoiding repeated r.viewshed calls on
#               the full raster.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Orbiter LOS contact fraction for a circular relay orbit.
# % keyword: Planetary
# % keyword: Visibility
# % keyword: orbiter
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: body
# % label: Body descriptor JSON
# % required: yes
# %end

# %option
# % key: altitude_km
# % type: double
# % label: Orbiter altitude in km
# % answer: 100
# % required: no
# %end

# %option
# % key: inclination
# % type: double
# % label: Orbital inclination in degrees (90 = polar)
# % answer: 90
# % required: no
# %end

# %option
# % key: norbits
# % type: integer
# % label: Number of complete orbits to simulate
# % answer: 14
# % required: no
# %end

# %option
# % key: steps_per_orbit
# % type: integer
# % label: Sample points per orbit (angular resolution of ground track)
# % answer: 72
# % required: no
# %end

# %option
# % key: min_elev_deg
# % type: double
# % label: Minimum orbiter elevation above local horizon for contact (degrees)
# % answer: 5.0
# % required: no
# %end

# %option
# % key: horizon_step
# % type: double
# % label: Angular step for pre-computed horizon maps (degrees)
# % answer: 10.0
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: orbiter
# % required: no
# %end

# %option
# % key: nprocs
# % type: integer
# % label: Number of OpenMP threads for r.horizon
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
from p_lib import (body_params, cleanup_prefix, solar_elevation_azimuth,
                   region_center_geographic, precompute_horizons, interpolate_horizon)

_PREFIX_TMP = "pvis_orbit_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def ground_track(body_radius_m, altitude_m, inclination_deg,
                 n_orbits, steps_per_orbit,
                 solar_day_h, gravity_ms2):
    """
    Generate body-fixed sub-satellite (sub_lat_deg, sub_lon_deg) points for
    a circular orbit.

    Orbit plane is treated as inertially fixed; the body rotates eastward
    underneath at a constant rate ω_body = 360° / T_solar_day, so the
    sub-satellite longitude drifts WESTWARD in body-fixed coordinates by
    ω_body × Δt per step. The previous formulation used
    ``360 × n_orbits / (n_orbits + 0.1)`` per orbit, which silently
    collapses to ~357°/orbit regardless of body — producing essentially
    repeated ground tracks on any rotating body (Mars, Ceres, Europa,
    Titan), so the orbiter's ground track never sweeps the target region
    and ``orbiter_contact_fraction`` came out uniformly zero. On the Moon
    the bug was masked because the true drift is ~1°/orbit at low lunar
    altitude — small enough that the bug accidentally gave similar
    coverage to the correct formula.

    Orbital period from circular-orbit Kepler:
        T = 2π · √(r³ / μ), with μ = g_surf · R² and r = R + alt.
    """
    inc = math.radians(inclination_deg)
    r_orbit_m = body_radius_m + altitude_m
    mu = gravity_ms2 * body_radius_m ** 2           # GM
    orbital_period_s = 2.0 * math.pi * math.sqrt(r_orbit_m ** 3 / mu)
    solar_day_s = solar_day_h * 3600.0
    # Body-fixed longitude drift per step, westward (negative).
    omega_body_deg_per_s = 360.0 / solar_day_s
    dt_per_step_s = orbital_period_s / steps_per_orbit
    lon_drift_per_step_deg = -omega_body_deg_per_s * dt_per_step_s

    total_steps = n_orbits * steps_per_orbit
    track = []
    for k in range(total_steps):
        u = 2 * math.pi * k / steps_per_orbit          # argument of latitude
        # Position in orbital plane (inertial frame, before body-rotation)
        x = math.cos(u)
        y = math.sin(u) * math.cos(inc)
        z = math.sin(u) * math.sin(inc)
        # Body-fixed longitude = inertial longitude − ω_body·t
        lon = math.degrees(math.atan2(y, x)) + k * lon_drift_per_step_deg
        lat = math.degrees(math.asin(z))
        track.append((lat, lon % 360))
    return track


def main():
    opt_dem       = options["dem"]
    opt_body      = options["body"]
    opt_alt_km    = float(options["altitude_km"])
    opt_inc       = float(options["inclination"])
    opt_norbits   = int(options["norbits"])
    opt_spo       = int(options["steps_per_orbit"])
    opt_min_elev  = float(options["min_elev_deg"])
    opt_hor_step  = float(options["horizon_step"])
    opt_pfx       = options["prefix"]
    opt_nprocs    = int(options.get("nprocs") or 1)
    opt_memory    = int(options.get("memory") or 300)

    body = body_params(opt_body)
    pid  = os.getpid()
    R    = body.get("semi_major_axis_m", 1737400)
    alt_m = opt_alt_km * 1000.0

    # ── 1. Pre-compute horizon maps ───────────────────────────────────────
    gs.message(f"Pre-computing horizon maps (step={opt_hor_step}°)…")
    hor_base = f"{_PREFIX_TMP}hor_{pid}"
    horizons = precompute_horizons(opt_dem, hor_base, opt_hor_step, body=body,
                                   nprocs=opt_nprocs, memory=opt_memory)

    # ── 2. Simulate ground track ──────────────────────────────────────────
    gs.message(f"Simulating {opt_norbits} orbits ({opt_spo} steps each)…")
    track = ground_track(R, alt_m, opt_inc, opt_norbits, opt_spo,
                         solar_day_h=body.get("solar_day_hours", 24.0),
                         gravity_ms2=body.get("gravity_ms2", 1.62))

    # Region centre for elevation angle approximation
    ctr_lat, ctr_lon = region_center_geographic()

    # ── 3. Accumulate contact counts ─────────────────────────────────────
    # Vectorised version: gather all contributing-step contributions in a
    # pure-Python pass, then run a SINGLE mapcalc summing all per-step
    # if(...) terms. This collapses what used to be ~3 mapcalc calls per
    # step (490+ steps → ~1500 grass subprocesses) into one expression.
    accum = f"{_PREFIX_TMP}accum_{pid}"
    sin_clat = math.sin(math.radians(ctr_lat))
    cos_clat = math.cos(math.radians(ctr_lat))
    half_pi_R = R * math.pi / 2.0

    # horizons dict is keyed on CCW-from-east azimuths matching r.horizon's
    # NNN_F suffix; bake the conversion-and-bracketing inline for speed.
    hor_fallback = next(iter(horizons.values()))
    terms = []
    n_used = 0
    for sub_lat, sub_lon in track:
        sub_lat_r = math.radians(sub_lat)
        cos_dlon = math.cos(math.radians(sub_lon - ctr_lon))
        cos_zen = (sin_clat * math.sin(sub_lat_r)
                   + cos_clat * math.cos(sub_lat_r) * cos_dlon)
        if cos_zen > 1.0:  cos_zen = 1.0
        elif cos_zen < -1.0: cos_zen = -1.0
        nadir_dist_m = R * math.acos(cos_zen)
        if nadir_dist_m > half_pi_R:
            continue
        el_geom = math.degrees(
            math.atan2(alt_m - R * (1 - cos_zen), nadir_dist_m + 1e-9))
        if el_geom < opt_min_elev:
            continue
        _, orb_az = solar_elevation_azimuth(ctr_lat, ctr_lon,
                                             sub_lat, sub_lon)

        # Mirror interpolate_horizon: compass(CW-from-N) → CCW-from-east.
        az_ccw = (90.0 - orb_az) % 360.0
        az_lo  = (int(az_ccw / opt_hor_step) * opt_hor_step) % 360.0
        az_hi  = (az_lo + opt_hor_step) % 360.0
        w      = (az_ccw - az_lo) / opt_hor_step
        m_lo   = horizons.get(az_lo, hor_fallback)
        m_hi   = horizons.get(az_hi, hor_fallback)

        # Interpolated horizon inlined directly into the comparison so we
        # never materialise a per-step temporary raster.
        terms.append(
            f"if({el_geom:.6f} > (1.0-{w:.6f})*{m_lo} + "
            f"{w:.6f}*{m_hi} + {opt_min_elev:.6f}, 1, 0)")
        n_used += 1

    gs.message(f"Orbit steps contributing: {n_used}/{len(track)}")

    if n_used == 0:
        gs.mapcalc(f"{accum} = 0", overwrite=True, quiet=True)
    else:
        # One mapcalc that reads each horizon raster once and emits the
        # contact count. Expression length is bounded by n_used (~500
        # terms × ~80 chars = ~40 KB), well under mapcalc's stdin limit.
        # If a future workload pushes much higher, chunk into batches.
        gs.mapcalc(f"{accum} = " + " + ".join(terms),
                   overwrite=True, quiet=True)

    # ── 4. Contact fraction ───────────────────────────────────────────────
    frac_out = f"{opt_pfx}_contact_fraction"
    if n_used > 0:
        gs.mapcalc(f"{frac_out} = double({accum}) / {n_used}.0",
                   overwrite=gs.overwrite(), quiet=True)
    else:
        gs.mapcalc(f"{frac_out} = 0.0",
                   overwrite=gs.overwrite(), quiet=True)

    gs.run_command("r.support", map=frac_out,
                   title=f"Orbiter contact fraction [{opt_alt_km:.0f}km, {opt_inc:.0f}° incl.]",
                   units="fraction", source1="p.visibility.orbiter", quiet=True)
    gs.run_command("r.colors", map=frac_out, color="bcyr", quiet=True)

    # passes per day
    orbital_period_h = 2 * math.pi * (R + alt_m) / math.sqrt(
        body.get("gravity_ms2", 1.62) * R**2 / (R + alt_m)) / 3600
    solar_day_h = body.get("solar_day_hours", 24.0)
    passes_per_day = solar_day_h / orbital_period_h
    gs.message(f"Orbital period ≈ {orbital_period_h:.2f} h → "
               f"≈{passes_per_day:.1f} passes/day")

    # ── clean up ──────────────────────────────────────────────────────────
    gs.run_command("g.remove", type="raster",
                   name=",".join(list(horizons.values()) + [accum]),
                   flags="f", quiet=True)

    gs.message(f"Output maps:\n  {frac_out}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
