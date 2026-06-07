#!/usr/bin/env python3
############################################################################
# MODULE:       p.spice.subpoint
# PURPOSE:      Print the sub-solar and/or sub-observer (sub-Earth) point of
#               a body at a given UTC epoch, using the adopted CSPICE library.
#               A teaching/debugging utility and the shared SPICE backend the
#               illumination and visibility modules use.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Sub-solar / sub-observer point of a body at a UTC epoch via SPICE.
# % keyword: Planetary
# % keyword: SPICE & Ephemeris
# % keyword: subpoint
# %end

# %option
# % key: epoch
# % type: string
# % label: UTC epoch (ISO-8601, e.g. 2028-06-01T00:00:00)
# % required: yes
# %end

# %option
# % key: point
# % type: string
# % label: Which sub-point(s) to report
# % options: sun,observer,both
# % answer: both
# % required: no
# %end

# %option G_OPT_F_INPUT
# % key: meta
# % label: Meta-kernel to load (default: the mapset's configured meta-kernel)
# % required: no
# %end

# %option
# % key: target
# % type: string
# % label: Target body (default: mapset config, e.g. MOON)
# % required: no
# %end

# %option
# % key: frame
# % type: string
# % label: Body-fixed frame (default: mapset config, e.g. MOON_ME)
# % required: no
# %end

# %option
# % key: observer
# % type: string
# % label: Observer body for the sub-observer point (default: mapset config or EARTH)
# % required: no
# %end

# %flag
# % key: g
# % description: Print in shell/script style (key=value)
# %end

import os
import sys

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_spice


def main():
    opt_epoch    = options["epoch"]
    opt_point    = options["point"]
    opt_meta     = options["meta"]
    opt_target   = options["target"]
    opt_frame    = options["frame"]
    opt_observer = options["observer"]
    flag_g       = flags["g"]

    if not p_spice.spice_available():
        gs.fatal("libcspice.so could not be loaded. Install the cspice "
                 "library or set $CSPICE_LIB.")

    cfg = {}
    if opt_meta:
        meta = os.path.abspath(opt_meta)
        if not os.path.isfile(meta):
            gs.fatal(f"Meta-kernel not found: {meta}")
        p_spice.kclear()
        p_spice.furnsh(meta)
    else:
        cfg = p_spice.read_mapset_config()
        if not cfg.get("P_SPICE_META"):
            gs.fatal("No meta-kernel given and none configured for this "
                     "mapset. Run p.in.spice then p.spice.config, or pass "
                     "meta=.")
        p_spice.activate_from_mapset()

    target   = opt_target   or cfg.get("P_SPICE_TARGET")   or "MOON"
    frame    = opt_frame    or cfg.get("P_SPICE_FRAME")    or "IAU_MOON"
    observer = opt_observer or cfg.get("P_SPICE_OBSERVER") or "EARTH"

    try:
        et = p_spice.str2et(opt_epoch)
        results = {}
        if opt_point in ("sun", "both"):
            slat, slon = p_spice.subsolar_point(target, frame, et)
            results["subsolar"] = (slat, slon)
        if opt_point in ("observer", "both"):
            olat, olon = p_spice.subobserver_point(target, frame, et, observer)
            results["subobserver"] = (olat, olon)
    except p_spice.SpiceError as e:
        gs.fatal(f"SPICE call failed: {str(e).splitlines()[0]}")

    if flag_g:
        gs.message(f"target={target}")
        gs.message(f"frame={frame}")
        gs.message(f"et={et:.6f}")
        if "subsolar" in results:
            gs.message(f"subsolar_lat={results['subsolar'][0]:.6f}")
            gs.message(f"subsolar_lon={results['subsolar'][1]:.6f}")
        if "subobserver" in results:
            gs.message(f"observer={observer}")
            gs.message(f"subobserver_lat={results['subobserver'][0]:.6f}")
            gs.message(f"subobserver_lon={results['subobserver'][1]:.6f}")
    else:
        gs.message(f"Body {target}  frame {frame}  epoch {opt_epoch} (UTC)")
        if "subsolar" in results:
            slat, slon = results["subsolar"]
            gs.message(f"  Sub-solar point:    lat {slat:+8.3f}°  lon {slon:8.3f}° E")
        if "subobserver" in results:
            olat, olon = results["subobserver"]
            gs.message(f"  Sub-{observer.lower()} point: "
                       f"lat {olat:+8.3f}°  lon {olon:8.3f}° E")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
