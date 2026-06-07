#!/usr/bin/env python3
############################################################################
# MODULE:       p.spice.config
# PURPOSE:      Set or show the active SPICE configuration for the current
#               GRASS mapset: the meta-kernel to load, the target body, the
#               body-fixed frame, and the Earth observer. Stored in the
#               mapset VAR file (g.gisenv store=mapset) so all p.* modules
#               read one source of truth. Can auto-detect the body and frame
#               from the Location CRS.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Set or show the per-mapset SPICE configuration (meta-kernel, target body, body-fixed frame, observer).
# % keyword: Planetary
# % keyword: SPICE & Ephemeris
# % keyword: configuration
# %end

# %option G_OPT_F_INPUT
# % key: meta
# % label: SPICE meta-kernel (.tm) to activate for this mapset
# % required: no
# %end

# %option
# % key: target
# % type: string
# % label: SPICE target body name (e.g. MOON, MARS)
# % required: no
# %end

# %option
# % key: frame
# % type: string
# % label: Body-fixed reference frame (e.g. MOON_ME, IAU_MOON, IAU_MARS)
# % required: no
# %end

# %option
# % key: observer
# % type: string
# % label: Observer body for sub-observer/visibility (default EARTH)
# % required: no
# %end

# %flag
# % key: p
# % description: Print the current mapset SPICE configuration and exit
# %end

# %flag
# % key: a
# % description: Auto-detect target body and body-fixed frame from the Location CRS
# %end

import os
import sys

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_spice

# Mapset VAR keys
K_META     = "P_SPICE_META"
K_TARGET   = "P_SPICE_TARGET"
K_FRAME    = "P_SPICE_FRAME"
K_OBSERVER = "P_SPICE_OBSERVER"

ALL_KEYS = [K_META, K_TARGET, K_FRAME, K_OBSERVER]

# Mean radius (m) → (target body, default body-fixed frame). Tolerance applied.
RADIUS_BODY = [
    (1737400.0, "MOON",    "MOON_ME"),
    (3396190.0, "MARS",    "IAU_MARS"),
    (2439700.0, "MERCURY", "IAU_MERCURY"),
    (6051800.0, "VENUS",   "IAU_VENUS"),
    (1561000.0, "EUROPA",  "IAU_EUROPA"),
    (2574700.0, "TITAN",   "IAU_TITAN"),
]


def _get(key):
    val = gs.read_command("g.gisenv", get=key, store="mapset").strip()
    return val or None


def _set(key, value):
    gs.run_command("g.gisenv", set=f"{key}={value}", store="mapset")


def _unset(key):
    gs.run_command("g.gisenv", unset=key, store="mapset")


def _print_config():
    gs.message("Current mapset SPICE configuration:")
    for k in ALL_KEYS:
        gs.message(f"  {k:18s} = {_get(k) or '(unset)'}")


def _detect_from_crs():
    """Return (target, frame) inferred from the Location's CRS radius."""
    info = gs.parse_command("g.proj", flags="g")
    a = info.get("a")
    if a is None:
        gs.fatal("Cannot read semi-major axis 'a' from the Location CRS "
                 "(g.proj -g). Specify target= and frame= explicitly.")
    a = float(a)
    for radius, body, frame in RADIUS_BODY:
        if abs(a - radius) <= max(2000.0, 0.001 * radius):
            return body, frame
    gs.fatal(f"Could not match CRS semi-major axis {a:.1f} m to a known body. "
             "Specify target= and frame= explicitly.")


def _frame_from_meta(meta_path):
    """Best-effort: read the frame name p.in.spice recorded in the meta-kernel
    comment block ('frame to pass to the planetary modules: NAME.')."""
    try:
        with open(meta_path) as f:
            for line in f:
                low = line.lower()
                if "frame to pass to the planetary modules:" in low:
                    return line.split(":")[-1].strip().rstrip(".") or None
    except OSError:
        pass
    return None


def main():
    opt_meta     = options["meta"]
    opt_target   = options["target"]
    opt_frame    = options["frame"]
    opt_observer = options["observer"]
    flag_print   = flags["p"]
    flag_auto    = flags["a"]

    if flag_print and not any([opt_meta, opt_target, opt_frame, opt_observer,
                               flag_auto]):
        _print_config()
        return

    # Auto-detect first; explicit options below override it.
    if flag_auto:
        body, frame = _detect_from_crs()
        gs.message(f"Auto-detected from CRS: target={body}, frame={frame}")
        _set(K_TARGET, body)
        _set(K_FRAME, frame)

    if opt_meta:
        meta = os.path.abspath(opt_meta)
        if not os.path.isfile(meta):
            gs.fatal(f"Meta-kernel not found: {meta}")
        _set(K_META, meta)
        # If frame still unset, try to take it from the meta-kernel comment.
        if not opt_frame and not _get(K_FRAME):
            mf = _frame_from_meta(meta)
            if mf:
                _set(K_FRAME, mf)
                gs.message(f"Frame taken from meta-kernel: {mf}")

    if opt_target:
        _set(K_TARGET, opt_target)
    if opt_frame:
        _set(K_FRAME, opt_frame)
    if opt_observer:
        _set(K_OBSERVER, opt_observer)
    elif not _get(K_OBSERVER):
        _set(K_OBSERVER, "EARTH")

    # Sanity: warn (not fatal) if the library or kernel cannot be loaded yet.
    meta = _get(K_META)
    if meta and p_spice.spice_available():
        try:
            p_spice.kclear()
            p_spice.furnsh(meta)
            target = _get(K_TARGET) or "MOON"
            frame  = _get(K_FRAME) or "IAU_MOON"
            et = p_spice.str2et("2000-01-01T12:00:00")
            lat, lon = p_spice.subsolar_point(target, frame, et)
            gs.message(f"Verified: sub-solar of {target} in {frame} at J2000 "
                       f"= lat {lat:.3f}, lon {lon:.3f}.")
        except p_spice.SpiceError as e:
            gs.warning(f"Configuration stored, but a test SPICE call failed: "
                       f"{str(e).splitlines()[0]}")
    elif meta and not p_spice.spice_available():
        gs.warning("Configuration stored, but libcspice.so could not be "
                   "loaded (set $CSPICE_LIB or install the cspice library).")

    _print_config()


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
