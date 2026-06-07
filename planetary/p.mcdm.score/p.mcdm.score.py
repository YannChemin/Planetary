#!/usr/bin/env python3
############################################################################
# MODULE:       p.mcdm.score
# PURPOSE:      Two-phase multi-criteria suitability scoring.
#               Phase 1: apply hard binary exclusion masks.
#               Phase 2: score remaining pixels with weighted linear
#                        combination (WLC) and/or TOPSIS (via r.mcda.topsis).
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Two-phase MCDM suitability scoring for planetary landing sites.
# % keyword: Planetary
# % keyword: Decision Support
# % keyword: MCDM
# % keyword: TOPSIS
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: slope
# % label: Normalised slope hazard raster [0=safe, 1=hazardous]
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: roughness
# % label: Normalised roughness hazard raster [0=safe, 1=hazardous]
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: illumination
# % label: Illumination fraction raster [0=dark, 1=always lit]
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: earth_vis
# % label: Earth visibility fraction raster [0=never, 1=always]
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: orbiter_vis
# % label: Orbiter/relay contact fraction raster [0=never, 1=always]
# % required: no
# %end

# %option G_OPT_R_INPUT
# % key: science
# % label: Science suitability raster [0=low, 1=high]
# % required: no
# %end

# %option
# % key: exclusion_masks
# % type: string
# % label: Comma-separated hard exclusion mask rasters (1=excluded)
# % required: no
# %end

# %option
# % key: weights
# % type: string
# % label: Criterion weights: slope,roughness,illumination,earth_vis,orbiter_vis,science
# % description: Must sum to 1.0. Missing criteria get weight 0. For backward compatibility a 5-element list (without orbiter_vis) is accepted; the 5th value is then taken as the science weight.
# % answer: 0.25,0.15,0.20,0.15,0.0,0.25
# % required: no
# %end

# %option
# % key: method
# % type: string
# % options: wlc,topsis,both
# % answer: both
# % label: Scoring method
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: suitability
# % required: no
# %end

import os
import sys
import atexit

import grass.script as gs
from grass.exceptions import CalledModuleError

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import cleanup_prefix, normalize_raster

_PREFIX_TMP = "pmcdm_score_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def build_exclusion_mask(mask_list, pid):
    """Union of all exclusion masks → a single combined mask (1=excluded)."""
    out = f"{_PREFIX_TMP}excl_{pid}"
    union_expr = " + ".join(mask_list)
    gs.mapcalc(f"{out} = if(({union_expr}) > 0, 1, 0)",
               overwrite=True, quiet=True)
    return out


def invert_raster(src, dst):
    """Normalize src to [0,1] then invert so high hazard → low suitability."""
    stats = gs.parse_command("r.univar", map=src, flags="g", quiet=True)
    vmin = float(stats["min"])
    vmax = float(stats["max"])
    if vmax == vmin:
        gs.mapcalc(f"{dst} = 0.0", overwrite=True, quiet=True)
        return
    gs.mapcalc(
        f"{dst} = ({vmax} - {src}) / ({vmax} - {vmin})",
        overwrite=True, quiet=True)


def wlc_score(criteria, weights, exclusion, prefix, pid):
    """Weighted linear combination of benefit criteria."""
    terms = [f"{w}*{c}" for w, c in zip(weights, criteria) if w > 0]
    if not terms:
        gs.fatal("No criteria with non-zero weights supplied.")
    expr = " + ".join(terms)
    out_raw = f"{_PREFIX_TMP}wlc_raw_{pid}"
    gs.mapcalc(f"{out_raw} = {expr}", overwrite=True, quiet=True)

    out = f"{prefix}_wlc"
    # Zero out excluded areas
    gs.mapcalc(
        f"{out} = if({exclusion} == 1, 0.0, {out_raw})",
        overwrite=gs.overwrite(), quiet=True)
    gs.run_command("g.remove", type="raster",
                   name=out_raw, flags="f", quiet=True)
    gs.run_command("r.colors", map=out, color="reds", quiet=True)
    gs.run_command("r.support", map=out,
                   title="WLC suitability score [0=unsuitable, 1=best]",
                   source1="p.mcdm.score", quiet=True)
    return out


def topsis_score(criteria, weights, exclusion, prefix, pid):
    """Call r.mcda.topsis (existing addon) for TOPSIS scoring."""
    out = f"{prefix}_topsis"
    try:
        # r.mcda.topsis expects all criteria as benefit type
        # weights as comma-separated list, preference as gain/gain/…
        prefs = ",".join(["gain"] * len(criteria))
        w_str = ",".join(str(w) for w in weights)
        gs.run_command(
            "r.mcda.topsis",
            criteria=",".join(criteria),
            weights=w_str,
            preference=prefs,
            output=out,
            quiet=True,
            overwrite=gs.overwrite(),
        )
        # Apply exclusion mask
        tmp = out + "_masked"
        gs.mapcalc(f"{tmp} = if({exclusion} == 1, 0.0, {out})",
                   overwrite=True, quiet=True)
        gs.run_command("g.rename", raster=f"{tmp},{out}",
                       quiet=True, overwrite=True)
        gs.run_command("r.colors", map=out, color="reds", quiet=True)
        gs.run_command("r.support", map=out,
                       title="TOPSIS suitability score [0=unsuitable, 1=best]",
                       source1="p.mcdm.score", quiet=True)
    except CalledModuleError:
        gs.warning("r.mcda.topsis failed; TOPSIS output not generated. "
                   "Install the r.mcda.topsis addon.")
        out = None
    return out


def main():
    opt_slope    = options["slope"]
    opt_rough    = options["roughness"]
    opt_illum    = options["illumination"]
    opt_evis     = options["earth_vis"]
    opt_orbiter  = options["orbiter_vis"]
    opt_science  = options["science"]
    opt_excl     = options["exclusion_masks"]
    opt_weights  = options["weights"]
    opt_method   = options["method"]
    opt_pfx      = options["prefix"]

    pid = os.getpid()

    raw_weights = [float(w.strip()) for w in opt_weights.split(",")]
    # Backward-compat: a 5-element weights list (the original layout)
    # is interpreted as slope,roughness,illumination,earth_vis,science —
    # the 5th value is the SCIENCE weight, not orbiter_vis. The 6-element
    # form is slope,roughness,illumination,earth_vis,orbiter_vis,science.
    if len(raw_weights) <= 5:
        ws = (raw_weights + [0.0] * 5)[:5]
        w_slope, w_rough, w_illum, w_evis, w_sci = ws
        w_orbiter = 0.0
    else:
        ws = (raw_weights + [0.0] * 6)[:6]
        w_slope, w_rough, w_illum, w_evis, w_orbiter, w_sci = ws

    # ── collect available criteria and corresponding weights ──────────────
    # Hazard criteria (slope, roughness) are COST → invert to benefit
    criteria_raw = []
    w_list       = []

    if opt_slope and w_slope > 0:
        inv = f"{_PREFIX_TMP}islope_{pid}"
        invert_raster(opt_slope, inv)
        criteria_raw.append(inv)
        w_list.append(w_slope)

    if opt_rough and w_rough > 0:
        inv = f"{_PREFIX_TMP}irough_{pid}"
        invert_raster(opt_rough, inv)
        criteria_raw.append(inv)
        w_list.append(w_rough)

    if opt_illum and w_illum > 0:
        criteria_raw.append(opt_illum)
        w_list.append(w_illum)

    if opt_evis and w_evis > 0:
        criteria_raw.append(opt_evis)
        w_list.append(w_evis)

    if opt_orbiter and w_orbiter > 0:
        criteria_raw.append(opt_orbiter)
        w_list.append(w_orbiter)

    if opt_science and w_sci > 0:
        criteria_raw.append(opt_science)
        w_list.append(w_sci)

    if not criteria_raw:
        gs.fatal("No criteria provided. Supply at least one of: "
                 "slope, roughness, illumination, earth_vis, orbiter_vis, "
                 "science.")

    # Normalise weights to sum = 1
    total_w = sum(w_list)
    w_norm  = [w / total_w for w in w_list]

    # ── Phase 1: build combined exclusion mask ────────────────────────────
    mask_inputs = []
    if opt_excl:
        mask_inputs = [m.strip() for m in opt_excl.split(",") if m.strip()]

    if mask_inputs:
        excl_map = build_exclusion_mask(mask_inputs, pid)
    else:
        # No hard exclusion → zero mask (nothing excluded)
        excl_map = f"{_PREFIX_TMP}excl0_{pid}"
        gs.mapcalc(f"{excl_map} = 0", overwrite=True, quiet=True)

    # Save as diagnostic output
    excl_out = f"{opt_pfx}_exclusion"
    gs.run_command("g.copy", raster=f"{excl_map},{excl_out}",
                   quiet=True, overwrite=gs.overwrite())
    gs.run_command("r.support", map=excl_out,
                   title="Combined hard exclusion mask (1=excluded)",
                   source1="p.mcdm.score", quiet=True)

    # ── Phase 2: scoring ─────────────────────────────────────────────────
    outputs = [excl_out]

    if opt_method in ("wlc", "both"):
        wlc = wlc_score(criteria_raw, w_norm, excl_map, opt_pfx, pid)
        outputs.append(wlc)

    if opt_method in ("topsis", "both"):
        tps = topsis_score(criteria_raw, w_norm, excl_map, opt_pfx, pid)
        if tps:
            outputs.append(tps)

    # ── final recommended map (average of available scoring methods) ──────
    score_maps = [o for o in outputs if o and o != excl_out]
    if len(score_maps) == 1:
        final = score_maps[0]
    elif len(score_maps) > 1:
        final = f"{opt_pfx}_final"
        avg_expr = "(" + " + ".join(score_maps) + f") / {len(score_maps)}"
        gs.mapcalc(f"{final} = {avg_expr}",
                   overwrite=gs.overwrite(), quiet=True)
        gs.run_command("r.colors", map=final, color="reds", quiet=True)
        gs.run_command("r.support", map=final,
                       title="Final suitability score (average of scoring methods)",
                       source1="p.mcdm.score", quiet=True)
        outputs.append(final)
    else:
        gs.fatal("No scoring method produced output.")

    # ── clean temporaries ─────────────────────────────────────────────────
    inv_maps = [c for c in criteria_raw if c.startswith(_PREFIX_TMP)]
    if inv_maps:
        gs.run_command("g.remove", type="raster",
                       name=",".join(inv_maps + [excl_map]),
                       flags="f", quiet=True)

    gs.message("Output maps:")
    for m in outputs:
        if m:
            gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
