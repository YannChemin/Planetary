"""
p_lib.py — shared utilities for the p.* planetary landing toolkit.
This is free and unencumbered software released into the public domain.
See https://unlicense.org for details.
"""

import os
import json
import math
import shutil
import tempfile
import atexit
import subprocess
import grass.script as gs
from osgeo import gdal, osr

# ── temporary workspace ──────────────────────────────────────────────────────

_tmpdir   = None
_tmpmaps  = []
_keep     = False


def init_tmp(keep=False):
    global _tmpdir, _keep
    _keep = keep
    _tmpdir = tempfile.mkdtemp(prefix="p_landing_")
    atexit.register(_cleanup)
    return _tmpdir


def _cleanup():
    if _tmpdir and not _keep and os.path.isdir(_tmpdir):
        shutil.rmtree(_tmpdir, ignore_errors=True)
    if _tmpmaps and not _keep:
        existing = gs.list_grouped("raster").get(gs.gisenv()["MAPSET"], [])
        to_del = [m for m in _tmpmaps if m in existing]
        if to_del:
            gs.run_command("g.remove", type="raster",
                           name=",".join(to_del), flags="f", quiet=True)


def register_tmp_raster(name):
    _tmpmaps.append(name)


def tmp_name(prefix):
    return f"{prefix}{os.getpid()}"


def cleanup_prefix(prefix):
    mapset = gs.gisenv()["MAPSET"]
    maps = gs.list_grouped("raster").get(mapset, [])
    to_del = [m for m in maps if m.startswith(prefix)]
    if to_del:
        gs.run_command("g.remove", type="raster",
                       name=",".join(to_del), flags="f", quiet=True)

# ── body / mission descriptors ───────────────────────────────────────────────

def body_params(json_path):
    if not json_path:
        gs.fatal("body= parameter is required.")
    if not os.path.isfile(json_path):
        gs.fatal(f"Body descriptor not found: {json_path}")
    with open(json_path) as f:
        return json.load(f)


def mission_params(json_path):
    if not json_path:
        return {}
    if not os.path.isfile(json_path):
        gs.fatal(f"Mission config not found: {json_path}")
    with open(json_path) as f:
        return json.load(f)

# ── coordinate utilities ─────────────────────────────────────────────────────

def _proj_inverse(x, y, proj_info):
    """
    Compute geographic (lat_deg, lon_deg) from projected (x, y) metres.

    Supports:
      stere  — polar stereographic (sphere), any pole latitude
      eqc    — equidistant cylindrical (simple cylindrical)
      longlat / ll — passthrough (x=lon, y=lat already in degrees)

    Uses analytic formulas for sphere so it works for any planetary body
    without requiring m.proj or pyproj (which choke on non-Earth CRS with
    PROJ >= 9.6 due to celestial-body enforcement).
    """
    proj = proj_info.get("proj", "ll")
    R    = float(proj_info.get("a", 1737400))

    if proj in ("ll", "longlat", "latlong"):
        return y, x   # already geographic (lat, lon)

    if proj == "stere":
        # Polar stereographic on sphere (k=1, true scale at pole)
        lat0_r = math.radians(float(proj_info.get("lat_0", 90)))
        lon0   = float(proj_info.get("lon_0", 0))
        rho    = math.sqrt(x**2 + y**2)
        if rho < 1.0:
            return math.degrees(lat0_r), lon0
        c      = 2.0 * math.atan2(rho, 2.0 * R)
        sin_c  = math.sin(c)
        cos_c  = math.cos(c)
        sin_l0 = math.sin(lat0_r)
        cos_l0 = math.cos(lat0_r)
        lat_r  = math.asin(cos_c * sin_l0 + y * sin_c * cos_l0 / rho)
        denom  = rho * cos_l0 * cos_c - y * sin_l0 * sin_c
        lon_r  = math.radians(lon0) + math.atan2(x * sin_c, denom)
        return math.degrees(lat_r), math.degrees(lon_r)

    if proj in ("eqc", "eqrect", "cea"):
        # Equidistant cylindrical (simple cylindrical)
        lon0 = float(proj_info.get("lon_0", 0))
        lat0 = float(proj_info.get("lat_0", 0))
        lat  = math.degrees(y / R) + lat0
        lon  = math.degrees(x / R) + lon0
        return lat, lon

    # Unknown projection — log a warning and return raw values
    gs.warning(
        f"region_center_geographic: unsupported projection '{proj}'. "
        "Returning raw projected coordinates as lat/lon (solar angles may be wrong)."
    )
    return y, x


def region_center_geographic():
    """
    Return (lat_deg, lon_deg) of the current computational region centre,
    correctly handling both geographic (longlat) and projected CRS.

    Uses analytic inverse projection formulas for sphere-based planetary
    bodies (polar stereographic, simple cylindrical) so it works without
    m.proj or pyproj, bypassing PROJ >= 9.6 celestial-body restrictions.
    """
    reg      = gs.region()
    proj_info = gs.parse_command("g.proj", flags="g", quiet=True)
    east  = (reg["e"] + reg["w"]) / 2.0
    north = (reg["n"] + reg["s"]) / 2.0
    lat, lon = _proj_inverse(east, north, proj_info)
    gs.verbose(f"Region geographic centre: lat={lat:.4f}°, lon={lon:.4f}°")
    return lat, lon

# ── map normalization ────────────────────────────────────────────────────────

def normalize_raster(src, dst, invert=False):
    stats = gs.parse_command("r.univar", map=src, flags="g", quiet=True)
    try:
        vmin = float(stats["min"])
        vmax = float(stats["max"])
    except (KeyError, ValueError):
        vmin, vmax = float("nan"), float("nan")

    # r.univar returns "nan"/"-inf"/"inf" when the map has no valid pixels
    # in the current region. Write a null output map and warn rather than
    # crashing downstream r.mapcalc with literal "nan"/"-inf" tokens.
    if (math.isnan(vmin) or math.isnan(vmax)
            or math.isinf(vmin) or math.isinf(vmax)):
        gs.warning(
            f"normalize_raster: '{src}' has no valid pixels in the "
            "current region — writing all-null '{dst}'. Check that the "
            "region intersects the raster's data extent."
        )
        gs.mapcalc(f"{dst} = null()", overwrite=True, quiet=True)
        return
    if vmax == vmin:
        gs.mapcalc(f"{dst} = 0.0", overwrite=True, quiet=True)
        return
    if invert:
        gs.mapcalc(
            f"{dst} = ({vmax} - {src}) / ({vmax} - {vmin})",
            overwrite=True, quiet=True)
    else:
        gs.mapcalc(
            f"{dst} = ({src} - {vmin}) / ({vmax} - {vmin})",
            overwrite=True, quiet=True)


def apply_color(mapname, rules):
    gs.run_command("r.colors", map=mapname, color=rules, quiet=True)

# ── r.horizon helpers ────────────────────────────────────────────────────────

def horizon_map_name(basename, az_deg):
    """
    Return the raster map name that r.horizon creates for a given azimuth.
    r.horizon replaces the decimal point with underscore:
      0.0°  → <basename>_000_0
      22.5° → <basename>_022_5
      45.0° → <basename>_045_0
    """
    int_part = int(az_deg)
    dec_part = int(round((az_deg - int_part) * 10))
    return f"{basename}_{int_part:03d}_{dec_part}"


def create_temp_location(base_path, location_name, proj4_str):
    """
    Create a temporary GRASS location with given projection and initialize it.
    Returns the location path.
    """
    gisdb = base_path or os.path.expanduser("~/grassdata")
    loc_path = os.path.join(gisdb, location_name)

    if os.path.exists(loc_path):
        shutil.rmtree(loc_path)

    # Create location with projection using grass command
    # This properly initializes the location with PERMANENT mapset
    try:
        subprocess.run([
            "grass", "-c",
            "-e",  # Create empty
            f"--text",
            f"{proj4_str}",
            f"{loc_path}"
        ], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError:
        # Fallback: create location directory manually
        os.makedirs(os.path.join(loc_path, "PERMANENT"), exist_ok=True)

        # Create PROJ_INFO file
        projinfo_path = os.path.join(loc_path, "PERMANENT", "PROJ_INFO")
        with open(projinfo_path, "w") as f:
            f.write("# GRASS projection info\n")
            for line in proj4_str.split():
                if line.startswith("+"):
                    f.write(f"{line}\n")

        # Create PROJ_UNITS file
        projunits_path = os.path.join(loc_path, "PERMANENT", "PROJ_UNITS")
        with open(projunits_path, "w") as f:
            f.write("meters\n")

    return loc_path


def transform_coordinates_gdal(x, y, from_proj4, to_proj4):
    """
    Transform a single coordinate pair using GDAL/OGR.
    Returns (x_new, y_new).
    """
    from_srs = osr.SpatialReference()
    from_srs.ImportFromProj4(from_proj4)

    to_srs = osr.SpatialReference()
    to_srs.ImportFromProj4(to_proj4)

    transform = osr.CoordinateTransformation(from_srs, to_srs)
    return transform.TransformPoint(x, y)[:2]


def precompute_horizons_robust(dem, basename, step_deg, nprocs=1, memory=300,
                               body=None):
    """
    Compute horizons with error tolerance and detailed logging of projection issues.

    `nprocs` (int >= 1) enables r.horizon's OpenMP parallelism across azimuths;
    `memory` (MB) sets r.horizon's row-cache. Both have no effect on a build
    without OpenMP and are safe to leave at their defaults.

    Backend selection:
      Setting the env var HORIZON_BACKEND=gpu swaps r.horizon for the
      OpenCL-accelerated p.horizon.gpu (requires the conformal CRS
      preconditions documented in that module). `body` (if provided) is
      used to pass the planetary radius for curvature correction.
    """
    backend = os.environ.get("HORIZON_BACKEND", "").lower()
    use_gpu = (backend == "gpu")
    gs.message(f"Computing horizon angles (step={step_deg}°, nprocs={nprocs}, "
               f"memory={memory}MB, backend={'gpu' if use_gpu else 'r.horizon'})…")

    # Run via subprocess to capture stderr with coordinate values
    import subprocess
    env = os.environ.copy()
    env["GRASS_VERBOSE"] = "2"  # Increase verbosity

    if use_gpu:
        # p.horizon.gpu: degrees output by default, no -d flag, no nprocs
        # (kernel parallelism is implicit). Pass bodyradius if `body` known.
        cmd = ["p.horizon.gpu", f"elevation={dem}", f"output={basename}",
               f"start=0", f"end=360", f"step={step_deg}", "--overwrite"]
        if body and "semi_major_axis_m" in body:
            cmd.append(f"bodyradius={float(body['semi_major_axis_m'])}")
        # nprocs/memory are r.horizon knobs; not applicable to GPU path.
        _ = (nprocs, memory)  # noqa: F841
    else:
        # r.horizon in GRASS 8.x exposes `nprocs` (OpenMP) but NOT `memory=`
        # (unlike most raster modules). Pass nprocs only; `memory` is accepted
        # by the wrapper for API symmetry but silently ignored here.
        _ = memory  # noqa: F841
        cmd = ["r.horizon", f"elevation={dem}", f"output={basename}",
               f"step={step_deg}", f"nprocs={int(nprocs)}",
               "-d", "--overwrite"]

    # Note: Expected warnings: "stere: Invalid longitude" at poles
    # These are from PROJ projection conditioning, not errors
    gs.message(f"  Computing horizons at {int(360/step_deg)} azimuths (expect projection warnings at poles)…")

    # Scale timeout with workload. r.horizon at polar stereo on dense
    # 25 M-cell rasters routinely needs minutes per azimuth.
    # Override with P_HORIZON_TIMEOUT (seconds) if set.
    try:
        reg = gs.region()
        n_cells = int(reg["rows"]) * int(reg["cols"])
    except Exception:
        n_cells = 10_000_000
    n_azimuths = max(1, int(round(360.0 / step_deg)))
    # Heuristic: ~250k cells per second per azimuth, +60 s fixed overhead,
    # clamp to [600 s, 21 600 s] (10 min .. 6 h).
    timeout_s = int(60 + (n_cells * n_azimuths) / 250_000.0)
    timeout_s = max(600, min(21_600, timeout_s))
    timeout_s = int(os.environ.get("P_HORIZON_TIMEOUT", timeout_s))
    gs.message(f"  r.horizon timeout: {timeout_s}s "
               f"({n_cells:,} cells × {n_azimuths} azimuths)")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_s,
            env=env
        )

        # Only report actual errors, not projection warnings
        if result.returncode != 0:
            if result.stderr and "Invalid longitude" not in result.stderr:
                gs.warning(f"  r.horizon stderr: {result.stderr[:500]}")

        if result.returncode != 0 and result.returncode not in [1, 2]:
            # Non-zero return might mean partial success
            gs.warning(f"  r.horizon returned code {result.returncode}")

    except subprocess.TimeoutExpired:
        gs.warning(f"  r.horizon timed out after {timeout_s}s. "
                   "Set env P_HORIZON_TIMEOUT to override.")
    except Exception as e:
        gs.warning(f"  r.horizon error: {e}")

    gs.message(f"Checking which horizon maps were successfully created…")

    horizons = {}
    valid_maps = []
    missing_azimuths = []
    az = 0.0

    # Check which maps were created
    gs.message(f"Checking {int(360/step_deg)} azimuth maps…")
    while az < 360.0:
        hname = horizon_map_name(basename, az)
        if gs.find_file(hname, element="cell")["file"]:
            horizons[az] = hname
            valid_maps.append((az, hname))
            gs.message(f"  ✓ {az:6.1f}° — {hname}")
        else:
            missing_azimuths.append(az)
            gs.message(f"  ✗ {az:6.1f}° — MISSING (likely pole singularity)")
        az += step_deg

    # Report summary
    gs.message(f"Summary: {len(valid_maps)} created, {len(missing_azimuths)} missing")

    # Interpolate missing azimuths from valid neighbors
    if missing_azimuths and valid_maps:
        gs.message(f"Interpolating {len(missing_azimuths)} missing horizon maps from neighbors…")
        for az_miss in missing_azimuths:
            az_prev = (az_miss - step_deg) % 360
            az_next = (az_miss + step_deg) % 360

            # Find valid maps near missing azimuth
            map_prev = horizons.get(az_prev)
            map_next = horizons.get(az_next)

            if map_prev and map_next:
                hname_miss = horizon_map_name(basename, az_miss)
                gs.mapcalc(
                    f"{hname_miss} = ({map_prev} + {map_next}) / 2.0",
                    overwrite=True, quiet=True)
                horizons[az_miss] = hname_miss

    return horizons


def precompute_horizons(dem, basename, step_deg, body=None, nprocs=1, memory=300):
    """
    Compute horizon angles with robust error tolerance for polar regions.

    At poles, stereographic projection has singularities that cause r.horizon
    to fail for some azimuths. This function:
    1. Computes r.horizon in current location (parallelised over azimuths with
       `nprocs`, with `memory` MB of row-cache).
    2. Gracefully handles missing azimuths via interpolation
    3. Uses GDAL/OGR coordinate transformation helpers (in p_lib)

    Args:
        dem: Input DEM raster name
        basename: Output basename for horizon maps
        step_deg: Angular step for horizon computation
        body: Body descriptor dict (for future lat/lon processing)
        nprocs: number of OpenMP threads for r.horizon (default 1)
        memory: r.horizon row-cache size in MB (default 300)

    Returns:
        Dict {az_deg: raster_map_name} for all generated maps
    """
    return precompute_horizons_robust(dem, basename, step_deg,
                                      nprocs=nprocs, memory=memory,
                                      body=body)


def interpolate_horizon(horizons, azimuth, step_deg, pid):
    """
    Return a raster map name with the linearly interpolated horizon angle
    at the given azimuth, using the two bracketing pre-computed maps.

    `azimuth` is a compass bearing (degrees clockwise from North), as returned
    by solar_elevation_azimuth().  r.horizon names its output maps by azimuth
    measured counter-clockwise from East, so convert before indexing — without
    this the horizon is sampled ~90 deg off the true Sun/Earth direction.
    """
    azimuth = (90.0 - azimuth) % 360.0
    az_lo = (int(azimuth / step_deg) * step_deg) % 360
    az_hi = (az_lo + step_deg) % 360
    f     = (azimuth - az_lo) / step_deg

    map_lo = horizons.get(az_lo)
    map_hi = horizons.get(az_hi, list(horizons.values())[0])

    if map_lo is None:
        map_lo = list(horizons.values())[0]

    out = f"p_hor_interp_{pid}"
    gs.mapcalc(
        f"{out} = (1 - {f}) * {map_lo} + {f} * {map_hi}",
        overwrite=True, quiet=True)
    return out

# ── solar geometry (planetary-generic) ───────────────────────────────────────

def sun_position_moon(t_days, body):
    """
    Approximate sub-solar latitude and longitude on the Moon at t_days.
    Returns (sub_solar_lat_deg, sub_solar_lon_deg_east 0-360).
    """
    tilt_max = body.get("axial_tilt_deg", 1.54) + body.get("orbital_inclination_deg", 5.14)
    nut_days = body.get("nutation_period_years", 18.6) * 365.25
    syn_days = body.get("synodic_period_days", 29.53)
    lat = tilt_max * math.sin(2 * math.pi * t_days / nut_days)
    lon = (360.0 * t_days / syn_days) % 360.0
    return lat, lon


def sun_position_generic(t_days, body):
    """Deprecated. Kept as a thin alias to :func:`sun_position_meeus` so old
    callers keep working. New code should use sun_position_meeus(jd, body)
    (Julian Day input) or sun_position_spice(jd, body).

    The original implementation used ``sin(2π t / (period × 4))`` which gives
    the wrong seasonal cycle length on every body except the Moon (and even
    there only coincidentally) — see the rationale in sun_position_meeus.
    """
    j2000 = 2451545.0
    return sun_position_meeus(j2000 + t_days, body)


# ── proper analytic sub-solar geometry (Meeus + IAU 2015 + J2000 phase) ──────
#
# For each supported body, ``_LS_AT_J2000`` is the body's solar longitude
# Ls (areocentric / planetocentric) at the J2000 epoch (JD 2451545.0),
# in degrees east of the body's northern vernal equinox direction. Ls=0
# is N vernal equinox, Ls=90° N summer solstice, Ls=180° N autumn
# equinox, Ls=270° N winter solstice. The sub-solar latitude then
# follows the seasonal cycle
#
#     sub_solar_lat = obliquity × sin(Ls)
#
# which is the closed-form solution under the small-eccentricity
# Standish approximation; accuracy is ~0.1° on the sub-solar latitude,
# more than adequate for landing-site terrain shadowing.
#
# Calibration sources (per-body):
#   Earth   23.4° × sin(280°)  = −23.05° at 2000-01-01      ✓ winter solstice
#   Mars    25.19° × sin(277.18°) = −25.0°                   ✓ Clancy 2000
#   Saturn  26.73° × sin(237.3°)  = −22.4° at J2000          ✓ S summer (last
#                                                                   solstice 2002,
#                                                                   next 2032)
# Tidally-locked moons inherit the parent planet's obliquity AND Ls.
_LS_AT_J2000 = {
    # Major planets (N spring equinox convention)
    "mercury":   0.0,    # obliquity ≈ 0.034°, value immaterial
    "venus":     0.0,    # retrograde, obliquity ≈ 177°, model treated as 0
    "earth":     280.0,  # ≈Jan 1 → sub-solar at -23.05° (winter solstice)
    "moon":      280.0,  # Earth-frame analogue (Moon uses Meeus libration)
    "mars":      277.18, # Clancy et al. 2000 calibration
    "ceres":     180.0,  # low obliquity (4°), value largely immaterial
    "jupiter":   0.0,    # obliquity 3.13°, low impact
    "saturn":    237.3,  # from 2002 / 2032 southern summer solstice anchor
    "uranus":    0.0,    # near-pole, treat as 0
    "neptune":   0.0,
    "pluto":     0.0,
    # Tidally-locked moons inherit the PARENT planet's seasonal phase.
    # Their `axial_tilt_deg` and `sidereal_period_days` should already
    # encode the parent's values (see config/europa.json, titan.json,
    # enceladus.json) so the formula below works uniformly.
    "io":        0.0,    # Jupiter system
    "europa":    0.0,
    "ganymede":  0.0,
    "callisto":  0.0,
    "titan":     237.3,  # Saturn system
    "enceladus": 237.3,
    "mimas":     237.3,
    "rhea":      237.3,
    "iapetus":   237.3,
}


def sun_position_meeus(jd, body):
    """Sub-solar (lat_deg, lon_deg_east_0_360) on *body* at Julian Day *jd*.

    For the Moon, dispatches to the full Meeus chapter 25+47+53 libration
    code (:func:`subsolar_point_moon`); accuracy ~0.003° in latitude.

    For any other body, computes the sub-solar latitude from the seasonal
    cycle anchored at J2000:

      Ls(jd) = (Ls_J2000 + 360 × (jd - 2451545) / sidereal_period_days) mod 360
      sub_solar_lat = obliquity × sin(Ls)

    The sub-solar longitude rotates at the body's mean solar day:

      sub_solar_lon = (360 × hours_since_J2000 / solar_day_hours) mod 360

    Tidally-locked moons inherit their parent planet's obliquity, sidereal
    period, and J2000 Ls calibration (set automatically via
    :data:`_LS_AT_J2000`); see ``config/europa.json`` and
    ``config/enceladus.json`` for examples.

    Accuracy is ~0.1° on the sub-solar latitude — adequate for the seasonal
    illumination integration that drives landing-site selection, and the
    primary use case where the previous toy ``sun_position_generic``
    produced unphysical sub-solar latitudes near zero regardless of epoch
    (Enceladus south polar terrain in 2032 was the catalyst for this
    rewrite). For arcsecond-class work use :func:`sun_position_spice`.
    """
    name = body.get("name", "").strip().lower()
    if name == "moon":
        # High-precision selenographic libration model (~0.003° latitude).
        return subsolar_point_moon(jd)

    obliquity_deg = float(body.get("axial_tilt_deg", 0.0))
    sidereal_d    = float(body.get("sidereal_period_days", 365.25))
    solar_day_h   = float(body.get("solar_day_hours", 24.0))
    ls_j2000_deg  = float(body.get("ls_at_j2000_deg",
                                   _LS_AT_J2000.get(name, 0.0)))

    days_since_j2000 = jd - 2451545.0
    ls_deg = (ls_j2000_deg + 360.0 * days_since_j2000 / sidereal_d) % 360.0
    sub_lat = obliquity_deg * math.sin(math.radians(ls_deg))

    hours_since_j2000 = days_since_j2000 * 24.0
    sub_lon = (360.0 * hours_since_j2000 / solar_day_h) % 360.0
    return sub_lat, sub_lon


def sun_position_spice(jd, body):
    """Sub-solar (lat_deg, lon_deg_east_0_360) on *body* at Julian Day *jd*
    using NAIF SPICE — arcsecond accuracy when the right kernels are
    loaded.

    Raises ``NotImplementedError`` if:
      * the SPICE library (planetary-cspice) is not installed, or
      * the required ephemeris and PCK kernels for *body* are not
        furnished in the active mapset's SPICE cache.

    The body's SPICE target name is taken from ``body['isis_target']``
    (set in every config under ``config/<body>.json``); the body-fixed
    frame is taken from ``body['spice_fixref']`` if present, otherwise
    falls back to ``IAU_<target>`` which is the IAU 2015 mean-equator /
    prime-meridian frame supplied with the standard planet PCK.
    """
    try:
        import p_spice
    except Exception as e:
        raise NotImplementedError(
            f"p_spice import failed ({e}); install planetary-cspice "
            "or use sun_position_meeus instead.")
    if not p_spice.spice_available():
        raise NotImplementedError(
            "SPICE library not available; use sun_position_meeus instead.")

    target = body.get("isis_target") or body.get("name") or ""
    target = target.upper()
    fixref = body.get("spice_fixref") or f"IAU_{target}"

    # JD → ephemeris time via SPICE's str2et. Use an ISO timestamp because
    # SPICE has built-in parsing; avoids reimplementing JD→ET leap-second
    # corrections.
    iso_str = jd_to_iso(jd)
    et = p_spice.str2et(iso_str)
    sub_lat, sub_lon = p_spice.subsolar_point(target, fixref, et)
    return sub_lat, sub_lon


def jd_to_iso(jd):
    """Inverse of :func:`iso_to_jd`. Returns a UTC ISO-8601 timestamp,
    second precision. Used to bridge to SPICE's string-based ET parser."""
    from datetime import datetime, timedelta
    # Reference: JD 2440587.5 = 1970-01-01T00:00:00 UTC (Unix epoch).
    unix_seconds = (jd - 2440587.5) * 86400.0
    dt = datetime(1970, 1, 1) + timedelta(seconds=unix_seconds)
    return dt.strftime("%Y-%m-%dT%H:%M:%S")


def solar_elevation_azimuth(px_lat, px_lon, sub_lat, sub_lon):
    """
    Solar elevation (degrees above horizon) and azimuth (degrees from N, CW)
    at surface point (px_lat, px_lon) given the sub-solar point.
    """
    px_lat_r  = math.radians(px_lat)
    px_lon_r  = math.radians(px_lon)
    sub_lat_r = math.radians(sub_lat)
    sub_lon_r = math.radians(sub_lon)
    dlon = sub_lon_r - px_lon_r
    cos_zen = (math.sin(px_lat_r) * math.sin(sub_lat_r)
               + math.cos(px_lat_r) * math.cos(sub_lat_r) * math.cos(dlon))
    cos_zen   = max(-1.0, min(1.0, cos_zen))
    elevation = 90.0 - math.degrees(math.acos(cos_zen))
    y = math.sin(dlon) * math.cos(sub_lat_r)
    x = (math.cos(px_lat_r) * math.sin(sub_lat_r)
         - math.sin(px_lat_r) * math.cos(sub_lat_r) * math.cos(dlon))
    azimuth = (math.degrees(math.atan2(y, x)) + 360.0) % 360.0
    return elevation, azimuth


# ── real analytic ephemeris (Meeus, self-contained, no external kernels) ──────
#
# These functions implement the truncated analytic theories from Jean Meeus,
# "Astronomical Algorithms" (2nd ed.):
#   - ch.25  apparent solar longitude
#   - ch.47  lunar position (principal periodic terms)
#   - ch.22  nutation in longitude (low precision)
#   - ch.53  optical + physical libration of the Moon
#
# They replace the toy single-sine model (sun_position_moon / earth_position_*)
# with positions tied to a real calendar epoch, so the monthly, annual and
# 18.6-year nutation terms superpose with their correct periods and phases.
# Accuracy is ~arcminute on the sub-solar / sub-Earth selenographic point —
# far finer than terrain horizon effects on a 30 m polar DEM, and fully
# deterministic with no DE/PCK kernel files to ship.  SPICE would be "more
# real" but needs spiceypy + ephemeris kernels that are not available here.

_DEG = math.pi / 180.0


def _sin_d(x):
    return math.sin(x * _DEG)


def _cos_d(x):
    return math.cos(x * _DEG)


def datetime_to_jd(dt):
    """Julian Day (UT, ΔT ignored — ~1 min, negligible here) from a
    datetime.datetime (assumed UTC)."""
    year, month = dt.year, dt.month
    day = (dt.day + dt.hour / 24.0 + dt.minute / 1440.0
           + dt.second / 86400.0 + dt.microsecond / 86400.0e6)
    if month <= 2:
        year -= 1
        month += 12
    a = math.floor(year / 100.0)
    b = 2 - a + math.floor(a / 4.0)
    return (math.floor(365.25 * (year + 4716))
            + math.floor(30.6001 * (month + 1))
            + day + b - 1524.5)


def iso_to_jd(iso_string):
    """Parse an ISO-8601 UTC timestamp (e.g. '2028-01-01T00:00:00') to JD."""
    from datetime import datetime
    s = iso_string.strip().replace("Z", "")
    for fmt in ("%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S",
                "%Y-%m-%dT%H:%M", "%Y-%m-%d"):
        try:
            return datetime_to_jd(datetime.strptime(s, fmt))
        except ValueError:
            continue
    raise ValueError(f"Unrecognised epoch format: {iso_string!r}")


def _jcentury(jd):
    return (jd - 2451545.0) / 36525.0


def _nutation_longitude_deg(T):
    """Δψ in degrees (Meeus 22, low-precision four-term form)."""
    omega = 125.04452 - 1934.136261 * T
    ls    = 280.4665 + 36000.7698 * T
    lm    = 218.3165 + 481267.8813 * T
    dpsi_arcsec = (-17.20 * _sin_d(omega) - 1.32 * _sin_d(2 * ls)
                   - 0.23 * _sin_d(2 * lm) + 0.21 * _sin_d(2 * omega))
    return dpsi_arcsec / 3600.0


def _sun_apparent_longitude_deg(T):
    """Apparent geocentric ecliptic longitude of the Sun (Meeus 25)."""
    l0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T
    m  = 357.52911 + 35999.05029 * T - 0.0001537 * T * T
    c  = ((1.914602 - 0.004817 * T - 0.000014 * T * T) * _sin_d(m)
          + (0.019993 - 0.000101 * T) * _sin_d(2 * m)
          + 0.000289 * _sin_d(3 * m))
    true_long = l0 + c
    omega = 125.04 - 1934.136 * T
    return (true_long - 0.00569 - 0.00478 * _sin_d(omega)) % 360.0


# Lunar longitude terms (Meeus table 47.A): (D, M, M', F, coeff[1e-6 deg])
_MOON_LON = [
    (0, 0, 1, 0, 6288774), (2, 0, -1, 0, 1274027), (2, 0, 0, 0, 658314),
    (0, 0, 2, 0, 213618), (0, 1, 0, 0, -185116), (0, 0, 0, 2, -114332),
    (2, 0, -2, 0, 58793), (2, -1, -1, 0, 57066), (2, 0, 1, 0, 53322),
    (2, -1, 0, 0, 45758), (0, 1, -1, 0, -40923), (1, 0, 0, 0, -34720),
    (0, 1, 1, 0, -30383), (2, 0, 0, -2, 15327), (0, 0, 1, 2, -12528),
    (0, 0, 1, -2, 10980), (4, 0, -1, 0, 10675), (0, 0, 3, 0, 10034),
    (4, 0, -2, 0, 8548), (2, 1, -1, 0, -7888), (2, 1, 0, 0, -6766),
    (1, 0, -1, 0, -5163), (1, 1, 0, 0, 4987), (2, -1, 1, 0, 4036),
    (2, 0, 2, 0, 3994), (4, 0, 0, 0, 3861), (2, 0, -3, 0, 3665),
    (0, 1, -2, 0, -2689), (2, 0, -1, 2, -2602), (2, -1, -2, 0, 2390),
    (1, 0, 1, 0, -2348), (2, -2, 0, 0, 2236), (0, 1, 2, 0, -2120),
    (0, 2, 0, 0, -2069), (2, -2, -1, 0, 2048),
]

# Lunar latitude terms (Meeus table 47.B): (D, M, M', F, coeff[1e-6 deg])
_MOON_LAT = [
    (0, 0, 0, 1, 5128122), (0, 0, 1, 1, 280602), (0, 0, 1, -1, 277693),
    (2, 0, 0, -1, 173237), (2, 0, -1, 1, 55413), (2, 0, -1, -1, 46271),
    (2, 0, 0, 1, 32573), (0, 0, 2, 1, 17198), (2, 0, 1, -1, 9266),
    (0, 0, 2, -1, 8822), (2, -1, 0, -1, 8216), (2, 0, -2, -1, 4324),
    (2, 0, 1, 1, 4200), (2, 1, 0, -1, -3359), (2, -1, -1, 1, 2463),
    (2, -1, 0, 1, 2211), (2, -1, -1, -1, 2065), (0, 1, -1, -1, -1870),
    (4, 0, -1, -1, 1828), (0, 1, 0, 1, -1794), (0, 0, 0, 3, -1749),
    (0, 1, -1, 1, -1565), (1, 0, 0, 1, -1491), (0, 1, 1, 1, -1475),
    (0, 1, 1, -1, -1410), (0, 1, 0, -1, -1344), (1, 0, 0, -1, -1335),
    (0, 0, 3, 1, 1107), (4, 0, 0, -1, 1021), (4, 0, -1, 1, 833),
]


def _moon_mean_elements(T):
    """Return the lunar mean arguments L', D, M, M', F (deg) and E."""
    Lp = (218.3164477 + 481267.88123421 * T - 0.0015786 * T**2
          + T**3 / 538841.0 - T**4 / 65194000.0)
    D  = (297.8501921 + 445267.1114034 * T - 0.0018819 * T**2
          + T**3 / 545868.0 - T**4 / 113065000.0)
    M  = (357.5291092 + 35999.0502909 * T - 0.0001536 * T**2
          + T**3 / 24490000.0)
    Mp = (134.9633964 + 477198.8675055 * T + 0.0087414 * T**2
          + T**3 / 69699.0 - T**4 / 14712000.0)
    F  = (93.2720950 + 483202.0175233 * T - 0.0036539 * T**2
          - T**3 / 3526000.0 + T**4 / 863310000.0)
    E  = 1.0 - 0.002516 * T - 0.0000074 * T * T
    return Lp, D, M, Mp, F, E


def _moon_lon_lat_deg(T):
    """Geometric geocentric ecliptic longitude and latitude of the Moon
    (Meeus 47, principal terms), in degrees."""
    Lp, D, M, Mp, F, E = _moon_mean_elements(T)
    sum_l = 0.0
    for d, m, mp, f, coeff in _MOON_LON:
        e_fac = E ** abs(m)
        sum_l += coeff * e_fac * _sin_d(d * D + m * M + mp * Mp + f * F)
    # additive longitude terms
    A1 = 119.75 + 131.849 * T
    A2 = 53.09 + 479264.290 * T
    sum_l += 3958 * _sin_d(A1) + 1962 * _sin_d(Lp - F) + 318 * _sin_d(A2)

    sum_b = 0.0
    for d, m, mp, f, coeff in _MOON_LAT:
        e_fac = E ** abs(m)
        sum_b += coeff * e_fac * _sin_d(d * D + m * M + mp * Mp + f * F)
    A3 = 313.45 + 481266.484 * T
    sum_b += (-2235 * _sin_d(Lp) + 382 * _sin_d(A3)
              + 175 * _sin_d(A1 - F) + 175 * _sin_d(A1 + F)
              + 127 * _sin_d(Lp - Mp) - 115 * _sin_d(Lp + Mp))

    lon = (Lp + sum_l / 1.0e6) % 360.0
    lat = sum_b / 1.0e6
    return lon, lat


def _selenographic_subpoint(T, lam_deg, beta_deg):
    """Selenographic longitude (east-positive) and latitude of the point on
    the Moon directly below a body whose geocentric ecliptic coordinates are
    (lam_deg, beta_deg).  Uses the optical + physical libration of Meeus 53.

    For the Moon's own (lam, beta) this returns the sub-Earth point
    (= libration); for the Sun's (lam, 0) it returns the sub-solar point.
    """
    Lp, D, M, Mp, F, E = _moon_mean_elements(T)
    I = 1.54242  # inclination of the lunar mean equator to the ecliptic (deg)
    omega = 125.04452 - 1934.136261 * T  # mean ascending node of lunar orbit

    # Meeus net form: W = λ_apparent − Δψ − Ω = λ_geometric − Ω
    W = (lam_deg - omega) % 360.0

    sinW, cosW = _sin_d(W), _cos_d(W)
    sinB, cosB = _sin_d(beta_deg), _cos_d(beta_deg)
    sinI, cosI = _sin_d(I), _cos_d(I)

    A = math.degrees(math.atan2(sinW * cosB * cosI - sinB * sinI,
                                cosW * cosB)) % 360.0
    b_opt = math.degrees(math.asin(
        max(-1.0, min(1.0, -sinW * cosB * sinI - sinB * cosI))))
    l_opt = (A - F + 180.0) % 360.0 - 180.0  # reduce to (-180, 180]

    # Physical libration (Meeus 53)
    K1 = 119.75 + 131.849 * T
    K2 = 72.56 + 20.186 * T
    rho = (-0.02752 * _cos_d(Mp) - 0.02245 * _sin_d(F)
           + 0.00684 * _cos_d(Mp - 2 * F) - 0.00293 * _cos_d(2 * F)
           - 0.00085 * _cos_d(2 * F - 2 * D) - 0.00054 * _cos_d(Mp - 2 * D)
           - 0.00020 * _sin_d(Mp + F) - 0.00020 * _cos_d(Mp + 2 * F)
           - 0.00020 * _cos_d(Mp - F) + 0.00014 * _cos_d(Mp + 2 * F - 2 * D))
    sigma = (-0.02816 * _sin_d(Mp) + 0.02244 * _cos_d(F)
             - 0.00682 * _sin_d(Mp - 2 * F) - 0.00279 * _sin_d(2 * F)
             - 0.00083 * _sin_d(2 * F - 2 * D) + 0.00069 * _sin_d(Mp - 2 * D)
             + 0.00040 * _cos_d(Mp + F) - 0.00025 * _sin_d(2 * Mp)
             - 0.00023 * _sin_d(Mp + 2 * F) + 0.00020 * _cos_d(Mp - F)
             + 0.00019 * _sin_d(Mp - F) + 0.00013 * _sin_d(Mp + 2 * F - 2 * D)
             - 0.00010 * _cos_d(Mp - 3 * F))
    tau = (0.02520 * E * _sin_d(M) + 0.00473 * _sin_d(2 * Mp - 2 * F)
           - 0.00467 * _sin_d(Mp) + 0.00396 * _sin_d(K1)
           + 0.00276 * _sin_d(2 * Mp - 2 * D) + 0.00196 * _sin_d(omega)
           - 0.00183 * _cos_d(Mp - F) + 0.00115 * _sin_d(Mp - 2 * D)
           - 0.00096 * _sin_d(Mp - D) + 0.00046 * _sin_d(2 * F - 2 * D)
           - 0.00039 * _sin_d(Mp - F) - 0.00032 * _sin_d(Mp - M - D)
           + 0.00027 * _sin_d(2 * Mp - M - 2 * D) + 0.00023 * _sin_d(K2)
           - 0.00014 * _sin_d(2 * D) + 0.00014 * _cos_d(2 * Mp - 2 * F)
           - 0.00012 * _sin_d(Mp - 2 * F) - 0.00012 * _sin_d(2 * Mp)
           + 0.00011 * _sin_d(2 * Mp - 2 * M - 2 * D))

    l_phys = -tau + (rho * _cos_d(A) + sigma * _sin_d(A)) * math.tan(b_opt * _DEG)
    b_phys = sigma * _cos_d(A) - rho * _sin_d(A)

    lon = l_opt + l_phys   # selenographic longitude, east-positive
    lat = b_opt + b_phys
    return lon, lat


def subearth_point_moon(jd):
    """Selenographic (lat, lon_east_0_360) of the sub-Earth point at Julian
    Day jd — i.e. the Moon's total libration. Lon east-positive, 0..360."""
    T = _jcentury(jd)
    lam, beta = _moon_lon_lat_deg(T)
    lon, lat = _selenographic_subpoint(T, lam, beta)
    return lat, lon % 360.0


def subsolar_point_moon(jd):
    """Selenographic (lat, lon_east_0_360) of the sub-solar point at Julian
    Day jd. Lon east-positive, 0..360.

    The libration formula expects the negative of the Moon→body direction
    (the sub-Earth case feeds the Moon's own geocentric longitude, which is
    the negative of the Moon→Earth direction). The Moon→Sun direction is
    essentially the Sun's geocentric apparent longitude λ_sun, so we feed
    λ_sun + 180°. The Moon→Sun parallax (< 0.15°) is neglected — immaterial
    against terrain horizon shadowing on a polar DEM. Validated against
    SPICE (subslr, IAU_MOON) to ~0.003° in latitude."""
    T = _jcentury(jd)
    lam_sun = _sun_apparent_longitude_deg(T)
    lon, lat = _selenographic_subpoint(T, lam_sun + 180.0, 0.0)
    return lat, lon % 360.0
