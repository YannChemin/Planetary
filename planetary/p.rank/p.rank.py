#!/usr/bin/env python3
############################################################################
# MODULE:       p.rank
# PURPOSE:      Extract top candidate landing sites from a suitability map,
#               rank them by composite score, run Monte Carlo weight
#               sensitivity analysis, and produce a ranked vector map plus
#               a machine-readable JSON report.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Rank candidate landing sites and quantify weight-sensitivity uncertainty.
# % keyword: Planetary
# % keyword: Decision Support
# % keyword: ranking
# % keyword: uncertainty
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: suitability
# % label: Final suitability raster [0=unsuitable, 1=best]
# % required: yes
# %end

# %option
# % key: criteria
# % type: string
# % label: Comma-separated criterion rasters (for per-site diagnostic statistics)
# % required: no
# %end

# %option
# % key: min_area_km2
# % type: double
# % label: Minimum candidate region area in km²
# % answer: 50.0
# % required: no
# %end

# %option
# % key: top_percentile
# % type: double
# % label: Suitability percentile threshold to extract candidates
# % answer: 85.0
# % required: no
# %end

# %option
# % key: n_candidates
# % type: integer
# % label: Maximum number of candidates to report
# % answer: 10
# % required: no
# %end

# %option
# % key: mc_samples
# % type: integer
# % label: Monte Carlo weight perturbation samples for sensitivity
# % answer: 200
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: rank
# % required: no
# %end

# %option G_OPT_F_OUTPUT
# % key: report
# % label: Output JSON report filename
# % answer: landing_ranking_report.json
# % required: no
# %end

import os
import sys
import json
import random
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import cleanup_prefix

_PREFIX_TMP = "prank_"


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def percentile_threshold(mapname, pct):
    """Return the value at the given percentile of a raster."""
    # r.quantile outputs lines like: "80%:28800000:0.9555"
    raw = gs.read_command(
        "r.quantile",
        input=mapname,
        percentiles=str(pct),
        quiet=True,
    )
    for line in raw.strip().split("\n"):
        parts = line.strip().split(":")
        if len(parts) == 3:
            try:
                return float(parts[2])
            except ValueError:
                pass
    # Fallback: linear interpolation from univar stats
    stats = gs.parse_command("r.univar", map=mapname, flags="g", quiet=True)
    vmin = float(stats["min"])
    vmax = float(stats["max"])
    return vmin + (pct / 100.0) * (vmax - vmin)


def zonal_stats(suit_map, clump_map, mapset):
    """Return dict {cat: {mean, std, n}} from r.univar -t (table with zones)."""
    raw = gs.read_command(
        "r.univar",
        map=suit_map,
        zones=clump_map,
        flags="t",          # -t: table output with zone column; NOT -g (conflicts)
        separator="pipe",
        quiet=True,
    )
    stats = {}
    lines = raw.strip().split("\n")
    if len(lines) < 2:
        return stats
    header = lines[0].split("|")
    for line in lines[1:]:
        vals = line.split("|")
        if len(vals) != len(header):
            continue
        row = dict(zip(header, vals))
        try:
            cat = int(row.get("zone", row.get("zone_cat", 0)))
            stats[cat] = {
                "mean":  float(row.get("mean",   0) or 0),
                "std":   float(row.get("stddev", 0) or 0),
                "n":     int(row.get("non_null_cells", row.get("n", 0)) or 0),
            }
        except (ValueError, KeyError):
            pass
    return stats


def min_cells_from_km2(min_km2):
    """Convert minimum area in km² to minimum cells at current resolution."""
    reg = gs.region()
    cell_area_km2 = (reg["nsres"] * reg["ewres"]) / 1e6
    return max(1, int(min_km2 / cell_area_km2))


def main():
    opt_suit     = options["suitability"]
    opt_criteria = options["criteria"]
    opt_min_area = float(options["min_area_km2"])
    opt_top_pct  = float(options["top_percentile"])
    opt_n_cand   = int(options["n_candidates"])
    opt_mc       = int(options["mc_samples"])
    opt_pfx      = options["prefix"]
    opt_report   = options["report"]

    pid     = os.getpid()
    mapset  = gs.gisenv()["MAPSET"]

    # ── 1. Threshold the suitability map ─────────────────────────────────
    # Work only on non-excluded pixels (suitability > 0) to avoid the
    # excluded-area zeros dominating the percentile calculation.
    gs.message(f"Computing {opt_top_pct}th-percentile threshold (non-excluded pixels)…")

    suit_nonzero = f"{_PREFIX_TMP}nonzero_{pid}"
    gs.mapcalc(
        f"{suit_nonzero} = if({opt_suit} > 0.0001, {opt_suit}, null())",
        overwrite=True, quiet=True)

    thr = percentile_threshold(suit_nonzero, opt_top_pct)
    gs.run_command("g.remove", type="raster",
                   name=suit_nonzero, flags="f", quiet=True)

    # If every pixel is excluded by the hard-exclusion mask
    # (suitability_wlc all-zero or all-null), r.quantile/r.univar return nan
    # and we cannot meaningfully threshold. Emit an empty report and exit
    # cleanly — this is a valid outcome for sites whose AOI is entirely
    # crater walls or other hazard terrain, not a pipeline error.
    import math
    if thr is None or math.isnan(thr):
        gs.warning(
            "All pixels excluded by hard-exclusion mask; "
            "no suitable terrain remains in this AOI. "
            "Writing empty candidate report.")
        empty_report = {
            "n_candidates":   0,
            "candidates":     [],
            "threshold":      None,
            "threshold_percentile": opt_top_pct,
            "min_area_km2":   opt_min_area,
            "note":           ("No non-excluded pixels — all terrain "
                               "failed the hard-exclusion mask."),
        }
        with open(opt_report, "w") as f:
            json.dump(empty_report, f, indent=2)
        gs.message(f"Report written to: {opt_report}")
        return 0

    gs.message(f"  Threshold = {thr:.4f}")

    high_suit = f"{_PREFIX_TMP}high_{pid}"
    gs.mapcalc(
        f"{high_suit} = if({opt_suit} >= {thr}, {opt_suit}, null())",
        overwrite=True, quiet=True)

    # ── 2. Clump connected high-suitability areas ─────────────────────────
    # r.clump groups pixels of the SAME integer value.  Suitability is a
    # float map, so we must first binarise it (1 = high suit, null = rest)
    # before clumping, so all adjacent high-suit pixels share value=1.
    gs.message("Clustering high-suitability regions (r.clump)…")
    high_suit_bin = f"{_PREFIX_TMP}high_bin_{pid}"
    gs.mapcalc(
        f"{high_suit_bin} = if({opt_suit} >= {thr}, 1, null())",
        overwrite=True, quiet=True)

    clump = f"{_PREFIX_TMP}clump_{pid}"
    gs.run_command("r.clump",
                   input=high_suit_bin,
                   output=clump,
                   quiet=True,
                   overwrite=True)
    gs.run_command("g.remove", type="raster",
                   name=high_suit_bin, flags="f", quiet=True)

    # ── 3. Filter by minimum area ─────────────────────────────────────────
    min_cells = min_cells_from_km2(opt_min_area)
    gs.message(f"Filtering candidates < {opt_min_area} km² ({min_cells} cells)…")

    # Count cells per clump
    raw_report = gs.read_command(
        "r.stats",
        input=clump,
        flags="cn",
        separator="space",
        quiet=True,
    )
    cat_counts = {}
    for line in raw_report.strip().split("\n"):
        parts = line.split()
        if len(parts) == 2:
            try:
                cat_counts[int(parts[0])] = int(parts[1])
            except ValueError:
                pass

    valid_cats = [c for c, n in cat_counts.items() if n >= min_cells]
    gs.message(f"  Candidates above size filter: {len(valid_cats)}")

    reg = gs.region()
    cell_area_km2 = (reg["nsres"] * reg["ewres"]) / 1e6

    if not valid_cats:
        if cat_counts:
            largest_cat, largest_cells = max(cat_counts.items(),
                                             key=lambda kv: kv[1])
            largest_area_km2 = largest_cells * cell_area_km2
        else:
            largest_cat, largest_cells, largest_area_km2 = None, 0, 0.0

        gs.warning(
            f"No candidate regions met the minimum area threshold "
            f"({opt_min_area} km²). Largest contiguous area found: "
            f"{largest_area_km2:.3f} km². "
            f"Lower top_percentile or min_area_km2 to obtain candidates."
        )

        report = {
            "suitability_map":      opt_suit,
            "threshold_percentile": opt_top_pct,
            "threshold_value":      round(thr, 6),
            "n_candidates_found":   0,
            "min_area_km2":         opt_min_area,
            "mc_samples":           opt_mc,
            "largest_candidate": {
                "clump_cat":  largest_cat,
                "area_km2":   round(largest_area_km2, 3),
                "n_clumps":   len(cat_counts),
            },
            "candidates": [],
            "status": "no_candidates_above_threshold",
        }
        with open(opt_report, "w") as f:
            json.dump(report, f, indent=2)
        gs.message(f"Report written to: {opt_report}")

        gs.run_command("g.remove", type="raster",
                       name=f"{high_suit},{clump}",
                       flags="f", quiet=True)
        return

    # ── 4. Per-candidate zonal statistics ─────────────────────────────────
    gs.message("Computing per-candidate statistics…")
    site_stats = zonal_stats(opt_suit, clump, mapset)

    # Rank by mean suitability (descending)
    ranked = sorted(
        [(cat, site_stats.get(cat, {})) for cat in valid_cats],
        key=lambda x: x[1].get("mean", 0),
        reverse=True,
    )
    ranked = ranked[:opt_n_cand]

    # ── 5. Monte Carlo weight sensitivity ────────────────────────────────
    sensitivity = {}
    criteria_maps = []
    if opt_criteria:
        criteria_maps = [c.strip() for c in opt_criteria.split(",") if c.strip()]

    if opt_mc > 0 and not criteria_maps and ranked:
        # Fallback path: no criterion maps were supplied (typical when called
        # from p.landing without forwarding the underlying layers). Perturb
        # each candidate's mean by a Gaussian of width = its within-region
        # std deviation and re-rank, so rank1_probability reflects intrinsic
        # within-patch variability rather than weight uncertainty.
        gs.message(f"Monte Carlo sensitivity ({opt_mc} samples, "
                   f"mean±std perturbation; no criteria= given)…")
        rank_counts = {cat: 0 for cat, _ in ranked}
        for _ in range(opt_mc):
            scores = {cat: random.gauss(st.get("mean", 0.0),
                                        max(st.get("std", 0.0), 1e-9))
                      for cat, st in ranked}
            top = max(scores, key=scores.get)
            rank_counts[top] += 1
        sensitivity = {str(cat): round(cnt / opt_mc, 4)
                       for cat, cnt in rank_counts.items()}

    if criteria_maps and opt_mc > 0:
        gs.message(f"Monte Carlo sensitivity ({opt_mc} samples, {len(criteria_maps)} criteria)…")

        # Collect per-criterion zonal means for each candidate
        crit_means = {cat: [] for cat, _ in ranked}
        valid_cats = [cat for cat, _ in ranked]

        for cmap in criteria_maps:
            raw = gs.read_command(
                "r.univar", map=cmap, zones=clump,
                flags="t", separator="pipe", quiet=True)
            lines = raw.strip().split("\n")
            if len(lines) < 2:
                # fallback: use suitability mean for all criteria
                for cat, st in ranked:
                    crit_means[cat].append(st.get("mean", 0))
                continue
            header = lines[0].split("|")
            for line in lines[1:]:
                vals = line.split("|")
                if len(vals) != len(header):
                    continue
                row = dict(zip(header, vals))
                try:
                    cat = int(row.get("zone", 0))
                    if cat in crit_means:
                        m = float(row.get("mean", 0) or 0)
                        crit_means[cat].append(m)
                except (ValueError, KeyError):
                    pass

        # Normalise criterion means to [0,1] across candidates
        n_crit = len(criteria_maps)
        for ci in range(n_crit):
            vals = [crit_means[c][ci] for c in valid_cats
                    if len(crit_means[c]) > ci]
            if not vals:
                continue
            vmin, vmax = min(vals), max(vals)
            dv = vmax - vmin if vmax > vmin else 1.0
            for cat in valid_cats:
                if len(crit_means[cat]) > ci:
                    crit_means[cat][ci] = (crit_means[cat][ci] - vmin) / dv

        rank_counts = {cat: 0 for cat, _ in ranked}
        for _ in range(opt_mc):
            raw_w = [random.random() for _ in range(n_crit)]
            tot   = sum(raw_w) or 1.0
            w     = [x / tot for x in raw_w]
            scores = {}
            for cat, _ in ranked:
                cm = crit_means.get(cat, [])
                if len(cm) == n_crit:
                    scores[cat] = sum(w[i] * cm[i] for i in range(n_crit))
                else:
                    scores[cat] = 0.0
            if scores:
                top = max(scores, key=scores.get)
                if top in rank_counts:
                    rank_counts[top] += 1

        sensitivity = {
            str(cat): round(cnt / opt_mc, 4)
            for cat, cnt in rank_counts.items()
        }

    # ── 6. Build ranked vector output ────────────────────────────────────
    gs.message("Building ranked vector map…")

    # Reclassify clump to keep only top candidates, values = rank position
    reclass_rules = ""
    for rank_pos, (cat, _) in enumerate(ranked, 1):
        reclass_rules += f"{cat} = {rank_pos}\n"
    reclass_rules += "* = NULL\n"

    ranked_rast = f"{_PREFIX_TMP}ranked_rast_{pid}"
    gs.write_command(
        "r.reclass",
        input=clump,
        output=ranked_rast,
        rules="-",
        overwrite=True,
        quiet=True,
        stdin=reclass_rules,
    )

    cand_vect = f"{opt_pfx}_candidates"
    gs.run_command(
        "r.to.vect",
        input=ranked_rast,
        output=cand_vect,
        type="area",
        quiet=True,
        overwrite=gs.overwrite(),
    )

    # Add attribute columns
    gs.run_command("v.db.addcolumn",
                   map=cand_vect,
                   columns="suit_mean DOUBLE PRECISION, "
                           "suit_std DOUBLE PRECISION, "
                           "area_km2 DOUBLE PRECISION, "
                           "rank1_prob DOUBLE PRECISION",
                   quiet=True)

    for rank_pos, (cat, st) in enumerate(ranked, 1):
        area_km2 = cat_counts.get(cat, 0) * cell_area_km2
        prob = sensitivity.get(str(cat), 0.0)
        gs.run_command(
            "v.db.update",
            map=cand_vect,
            column="suit_mean",
            value=str(round(st.get("mean", 0), 6)),
            where=f"cat={rank_pos}",
            quiet=True,
        )
        gs.run_command(
            "v.db.update",
            map=cand_vect,
            column="suit_std",
            value=str(round(st.get("std", 0), 6)),
            where=f"cat={rank_pos}",
            quiet=True,
        )
        gs.run_command(
            "v.db.update",
            map=cand_vect,
            column="area_km2",
            value=str(round(area_km2, 3)),
            where=f"cat={rank_pos}",
            quiet=True,
        )
        gs.run_command(
            "v.db.update",
            map=cand_vect,
            column="rank1_prob",
            value=str(prob),
            where=f"cat={rank_pos}",
            quiet=True,
        )

    # ── 7. Uncertainty raster ─────────────────────────────────────────────
    unc_out = f"{opt_pfx}_uncertainty"
    # Approximate uncertainty = std of suitability within each candidate region
    gs.run_command(
        "r.stats.zonal",
        base=ranked_rast,
        cover=opt_suit,
        method="stddev",
        output=unc_out,
        quiet=True,
        overwrite=gs.overwrite(),
    )
    gs.run_command("r.support", map=unc_out,
                   title="Suitability std dev (uncertainty proxy)",
                   source1="p.rank", quiet=True)

    # ── 8. JSON report ────────────────────────────────────────────────────
    report = {
        "suitability_map": opt_suit,
        "threshold_percentile": opt_top_pct,
        "threshold_value": round(thr, 6),
        "n_candidates_found": len(ranked),
        "min_area_km2": opt_min_area,
        "mc_samples": opt_mc,
        "candidates": [
            {
                "rank": rank_pos,
                "clump_cat": cat,
                "suit_mean": round(st.get("mean", 0), 6),
                "suit_std":  round(st.get("std",  0), 6),
                "area_km2":  round(cat_counts.get(cat, 0) * cell_area_km2, 3),
                "rank1_probability": sensitivity.get(str(cat), None),
            }
            for rank_pos, (cat, st) in enumerate(ranked, 1)
        ],
    }
    with open(opt_report, "w") as f:
        json.dump(report, f, indent=2)
    gs.message(f"Report written to: {opt_report}")

    # ── clean up ─────────────────────────────────────────────────────────
    # Remove ranked_rast (r.reclass child) BEFORE clump (its base map)
    # to avoid the "base map" GRASS warning.
    gs.run_command("g.remove", type="raster",
                   name=f"{high_suit},{ranked_rast}",
                   flags="f", quiet=True)
    gs.run_command("g.remove", type="raster",
                   name=clump, flags="f", quiet=True)

    gs.message("Output maps:")
    gs.message(f"  {cand_vect} (vector)")
    gs.message(f"  {unc_out} (raster)")

    gs.message(f"\nTop {len(ranked)} candidates:")
    for rank_pos, (cat, st) in enumerate(ranked, 1):
        area_km2 = cat_counts.get(cat, 0) * cell_area_km2
        gs.message(
            f"  #{rank_pos}: mean_suit={st.get('mean',0):.4f}, "
            f"area={area_km2:.1f} km²"
        )


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
