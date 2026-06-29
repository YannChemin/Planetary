#!/usr/bin/env python3
# % module
# % label: Create a GRASS GIS location for a planetary body.
# % description: Creates a new GRASS location configured with the correct ellipsoid (from the built-in body database or custom radii) and projection for a solar system body. Sets a sensible default region (full-globe or polar cap) at the requested resolution. Must be run from inside any existing GRASS session.
# % keyword: Planetary
# % keyword: Setup & Location
# % keyword: location
# % keyword: projection
# % keyword: ellipsoid
# % end

# %option
# % key: body
# % type: string
# % required: no
# % label: Planetary body (mars|moon|venus|mercury|titan|ceres|enceladus|europa|custom)
# % description: Reads semi-major/minor axes from the built-in body database. Use custom with semi_major= and semi_minor=.
# % options: mars,moon,venus,mercury,titan,ceres,enceladus,europa,custom
# % answer: mars
# %end

# %option
# % key: location
# % type: string
# % required: no
# % label: Name of the new GRASS location (default: body name)
# %end

# %option
# % key: projection
# % type: string
# % required: no
# % label: Map projection
# % options: latlong,eqc,sinu,north_stereo,south_stereo,merc,lcc,laea,ortho
# % answer: latlong
# % descriptions: latlong;Geographic lat/lon (degrees);eqc;Equidistant cylindrical (simple cylindrical) — standard planetary base map;sinu;Sinusoidal equal-area — low-distortion for full-globe maps;north_stereo;North polar stereographic;south_stereo;South polar stereographic;merc;Mercator — low-latitude strips;lcc;Lambert Conformal Conic — regional maps at mid-latitudes;laea;Lambert Azimuthal Equal-Area — hemispheric maps;ortho;Orthographic — visualisation
# %end

# %option
# % key: res
# % type: double
# % required: no
# % label: Resolution (degrees for latlong, metres for projected; 0=skip region setup)
# % answer: 0.01
# %end

# %option
# % key: lon_0
# % type: double
# % required: no
# % label: Central / reference longitude (degrees)
# % answer: 0.0
# %end

# %option
# % key: lat_0
# % type: double
# % required: no
# % label: Projection centre latitude (for lcc/laea/ortho/stere)
# % answer: 0.0
# %end

# %option
# % key: lat_1
# % type: double
# % required: no
# % label: First standard parallel for lcc (degrees)
# % answer: 30.0
# %end

# %option
# % key: lat_2
# % type: double
# % required: no
# % label: Second standard parallel for lcc (degrees)
# % answer: 60.0
# %end

# %option
# % key: semi_major
# % type: double
# % required: no
# % label: Custom equatorial radius in metres (body=custom)
# %end

# %option
# % key: semi_minor
# % type: double
# % required: no
# % label: Custom polar radius in metres (body=custom; default = semi_major)
# %end

# %flag
# % key: p
# % label: Print the PROJ.4 string that would be used and exit (dry run)
# %end

import os
import sys
import math
import glob
import json


# ── Body radius lookup ─────────────────────────────────────────────────────────
def _find_body_db():
    """Return ordered list of directories to search for <body>.json files."""
    here = os.path.dirname(os.path.abspath(__file__))
    gisbase = os.environ.get("GISBASE", "")
    return [
        os.path.join(gisbase, "bodies"),
        os.path.join(here, "..", "..", "bodies"),
        os.path.join(os.path.dirname(gisbase), "bodies"),
    ]


def _load_body(body_name):
    for d in _find_body_db():
        path = os.path.join(d, body_name.lower() + ".json")
        if os.path.exists(path):
            with open(path) as f:
                return json.load(f)
    return None


# ── PROJ.4 string builder ──────────────────────────────────────────────────────
def _build_proj4(proj, a, b, lon_0, lat_0, lat_1, lat_2):
    ab = f"+a={a:.1f} +b={b:.1f}"
    lo = f"+lon_0={lon_0:.6f}"

    if proj == "latlong":
        return f"+proj=longlat {ab} +no_defs"

    if proj == "eqc":
        return (f"+proj=eqc +lat_ts=0 +lat_0=0 {lo} +x_0=0 +y_0=0 "
                f"{ab} +units=m +no_defs")

    if proj == "sinu":
        return f"+proj=sinu {lo} +x_0=0 +y_0=0 {ab} +units=m +no_defs"

    if proj == "north_stereo":
        return (f"+proj=stere +lat_0=90 +lat_ts=90 {lo} +k=1 "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    if proj == "south_stereo":
        return (f"+proj=stere +lat_0=-90 +lat_ts=-90 {lo} +k=1 "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    if proj == "merc":
        return (f"+proj=merc +lat_ts=0 {lo} +k=1 "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    if proj == "lcc":
        return (f"+proj=lcc +lat_1={lat_1:.4f} +lat_2={lat_2:.4f} "
                f"+lat_0={lat_0:.4f} {lo} "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    if proj == "laea":
        return (f"+proj=laea +lat_0={lat_0:.4f} {lo} "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    if proj == "ortho":
        return (f"+proj=ortho +lat_0={lat_0:.4f} {lo} "
                f"+x_0=0 +y_0=0 {ab} +units=m +no_defs")

    raise ValueError(f"Unknown projection: {proj}")


# ── GRASS WIND / DEFAULT_WIND file ────────────────────────────────────────────
def _write_wind(path, proj_code, a, proj, res, lon_0, lat_0):
    """Write a GRASS WIND (region) file."""

    # Compute extent in native units
    if proj == "latlong":
        W, E, S, N = -180.0, 180.0, -90.0, 90.0
        ew_res = ns_res = res
        cols = max(1, int(round((E - W) / ew_res)))
        rows = max(1, int(round((N - S) / ns_res)))
        # Latlong WIND uses N/S/E/W suffixes
        north_s = f"{abs(N):.6f}{'N' if N >= 0 else 'S'}"
        south_s = f"{abs(S):.6f}{'N' if S >= 0 else 'S'}"
        east_s  = f"{abs(E):.6f}{'E' if E >= 0 else 'W'}"
        west_s  = f"{abs(W):.6f}{'E' if W >= 0 else 'W'}"
    else:
        # Full-body extent in metres for common projections
        pi_a = math.pi * a
        if proj in ("eqc", "sinu"):
            W, E = -pi_a, pi_a
            S, N = -pi_a / 2.0, pi_a / 2.0
        elif proj in ("north_stereo",):
            # From equator (lat=0) to north pole, ~2a radius
            W, E, S, N = -2 * a, 2 * a, -2 * a, 2 * a
        elif proj in ("south_stereo",):
            W, E, S, N = -2 * a, 2 * a, -2 * a, 2 * a
        elif proj == "merc":
            W, E = -pi_a, pi_a
            S, N = -pi_a * 0.7, pi_a * 0.7  # ~80° latitude
        elif proj in ("laea", "ortho"):
            r = math.sqrt(2) * a
            W, E, S, N = -r, r, -r, r
        elif proj == "lcc":
            W, E, S, N = -pi_a / 2, pi_a / 2, -pi_a / 4, pi_a / 4
        else:
            W, E, S, N = -pi_a, pi_a, -pi_a / 2, pi_a / 2
        ew_res = ns_res = res
        cols = max(1, int(round((E - W) / ew_res)))
        rows = max(1, int(round((N - S) / ns_res)))
        north_s = f"{N:.6f}"
        south_s = f"{S:.6f}"
        east_s  = f"{E:.6f}"
        west_s  = f"{W:.6f}"

    with open(path, "w") as f:
        f.write(f"proj:       {proj_code}\n")
        f.write("zone:       0\n")
        f.write(f"north:      {north_s}\n")
        f.write(f"south:      {south_s}\n")
        f.write(f"east:       {east_s}\n")
        f.write(f"west:       {west_s}\n")
        f.write(f"cols:       {cols}\n")
        f.write(f"rows:       {rows}\n")
        f.write(f"e-w resol:  {ew_res}\n")
        f.write(f"n-s resol:  {ns_res}\n")
        f.write("top:        1.000000000000000\n")
        f.write("bottom:     0.000000000000000\n")
        f.write(f"cols3:      {cols}\n")
        f.write(f"rows3:      {rows}\n")
        f.write("depths:     1\n")
        f.write(f"e-w resol3: {ew_res}\n")
        f.write(f"n-s resol3: {ns_res}\n")
        f.write("t-b resol:  1\n")


def main():
    try:
        import grass.script as gs
    except ImportError:
        gs = None

    def fatal(msg):
        if gs:
            gs.fatal(msg)
        else:
            print(f"ERROR: {msg}", file=sys.stderr)
            sys.exit(1)

    def message(msg):
        if gs:
            gs.message(msg)
        else:
            print(msg)

    # ── Parse options ─────────────────────────────────────────────────────────
    if gs:
        options, flags = gs.parser()
        body       = options.get("body", "mars")
        locname    = options.get("location", "")
        proj       = options.get("projection", "latlong")
        res        = float(options.get("res", "0.01"))
        lon_0      = float(options.get("lon_0", "0.0"))
        lat_0      = float(options.get("lat_0", "0.0"))
        lat_1      = float(options.get("lat_1", "30.0"))
        lat_2      = float(options.get("lat_2", "60.0"))
        semi_major = options.get("semi_major", "")
        semi_minor = options.get("semi_minor", "")
        dry_run    = flags.get("p", False)
    else:
        import argparse
        p = argparse.ArgumentParser()
        p.add_argument("--body", default="mars")
        p.add_argument("--location", default="")
        p.add_argument("--projection", default="latlong")
        p.add_argument("--res", type=float, default=0.01)
        p.add_argument("--lon_0", type=float, default=0.0)
        p.add_argument("--lat_0", type=float, default=0.0)
        p.add_argument("--lat_1", type=float, default=30.0)
        p.add_argument("--lat_2", type=float, default=60.0)
        p.add_argument("--semi_major", default="")
        p.add_argument("--semi_minor", default="")
        p.add_argument("-p", action="store_true")
        a = p.parse_args()
        body = a.body; locname = a.location; proj = a.projection
        res = a.res; lon_0 = a.lon_0; lat_0 = a.lat_0
        lat_1 = a.lat_1; lat_2 = a.lat_2
        semi_major = a.semi_major; semi_minor = a.semi_minor
        dry_run = a.p

    # ── Resolve body radii ────────────────────────────────────────────────────
    if body == "custom":
        if not semi_major:
            fatal("body=custom requires semi_major=")
        a_m = float(semi_major)
        b_m = float(semi_minor) if semi_minor else a_m
        body_label = f"custom (a={a_m:.1f} b={b_m:.1f})"
    else:
        db = _load_body(body)
        if db is None:
            fatal(f"Body '{body}' not found in body database. "
                  f"Use body=custom with semi_major= and semi_minor=.")
        a_m = float(db["semi_major_axis_m"])
        b_m = float(db["semi_minor_axis_m"])
        body_label = db.get("name", body)

    if not locname:
        locname = body.lower()

    # ── Build PROJ.4 string ───────────────────────────────────────────────────
    try:
        proj4 = _build_proj4(proj, a_m, b_m, lon_0, lat_0, lat_1, lat_2)
    except ValueError as e:
        fatal(str(e))

    if dry_run:
        print(f"body:       {body_label}")
        print(f"location:   {locname}")
        print(f"projection: {proj}")
        print(f"proj4:      {proj4}")
        return

    # ── Create the GRASS location ─────────────────────────────────────────────
    if not gs:
        fatal("Creating a GRASS location requires a running GRASS session. "
              "Use -p for a dry-run PROJ.4 string without GRASS.")

    gisenv = gs.gisenv()
    gisdbase = gisenv.get("GISDBASE", "")
    loc_path = os.path.join(gisdbase, locname)

    if os.path.exists(loc_path):
        fatal(f"Location '{locname}' already exists in GISDBASE '{gisdbase}'. "
              f"Choose a different location= name or remove it first.")

    message(f"Creating location '{locname}' for {body_label} ...")
    message(f"  projection: {proj}")
    message(f"  PROJ.4:     {proj4}")

    gs.run_command("g.proj", flags="c", proj4=proj4, location=locname, quiet=True)

    # ── Write DEFAULT_WIND (full-body region at requested resolution) ─────────
    perm_dir = os.path.join(loc_path, "PERMANENT")
    if not os.path.isdir(perm_dir):
        fatal(f"g.proj did not create PERMANENT mapset at '{perm_dir}'.")

    if res > 0:
        proj_code = 3 if proj == "latlong" else 99
        wind_path = os.path.join(perm_dir, "DEFAULT_WIND")
        _write_wind(wind_path, proj_code, a_m, proj, res, lon_0, lat_0)
        # WIND is a copy of DEFAULT_WIND in the PERMANENT mapset
        import shutil
        shutil.copy(wind_path, os.path.join(perm_dir, "WIND"))
        if proj == "latlong":
            message(f"  default region: full globe, resolution {res}°")
        else:
            message(f"  default region: full-body extent, resolution {res} m")

    message(f"Location '{locname}' created.")
    message(f"To start working: grass -c {loc_path}/PERMANENT")
    message(f"Or switch mapset: g.mapset -c mapset=PERMANENT location={locname}")


if __name__ == "__main__":
    main()
