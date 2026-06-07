#!/usr/bin/env python3
############################################################################
# MODULE:       p.rank.cross
# PURPOSE:      Cross-region ranking: ingest N per-region landing-site
#               reports produced by p.rank and emit a unified ranked table.
#               Allows tuning the relative weight of mean suitability vs.
#               patch area and aggregates per-region Monte-Carlo rank-1
#               probabilities via a Borda-count fallback.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Cross-region ranking of landing-site candidates from multiple p.rank reports.
# % keyword: Planetary
# % keyword: Decision Support
# % keyword: ranking
# % keyword: landing
# %end

# %option G_OPT_F_INPUT
# % key: reports
# % label: Comma-separated p.rank JSON report files (one per region)
# % description: Each file is read; its `candidates` array is merged into the cross ranking. Region id defaults to the report filename stem and can be overridden via `region_ids`.
# % required: yes
# %end

# %option
# % key: region_ids
# % type: string
# % label: Comma-separated region ids matching `reports` order (overrides filename stems)
# % required: no
# %end

# %option
# % key: weights
# % type: string
# % label: Composite-score weights as suit:<w>,area:<w>,borda:<w>
# % description: Each component is normalised to [0,1] across all merged candidates before being weighted. Component weights are renormalised to sum=1. Defaults: suit:0.6, area:0.25, borda:0.15.
# % answer: suit:0.6,area:0.25,borda:0.15
# % required: no
# %end

# %option
# % key: area_transform
# % type: string
# % options: linear,log,sqrt
# % answer: log
# % label: Pre-normalisation transform for area_km2
# % description: log compresses orders-of-magnitude differences (typical when a region has both ~1 km² and ~1000 km² patches); linear preserves them.
# % required: no
# %end

# %option
# % key: n_top
# % type: integer
# % label: Maximum candidates retained in the cross-ranked output
# % answer: 20
# % required: no
# %end

# %option G_OPT_F_OUTPUT
# % key: output
# % label: Output JSON report (unified cross-region ranking)
# % answer: cross_ranking_report.json
# % required: no
# %end

import os
import sys
import json
import math
import atexit

import grass.script as gs


_VALID_COMPONENTS = ("suit", "area", "borda")


def _parse_weights(spec):
    """Parse `suit:0.6,area:0.25,borda:0.15` into a renormalised dict."""
    w = {k: 0.0 for k in _VALID_COMPONENTS}
    for kv in spec.split(","):
        kv = kv.strip()
        if not kv:
            continue
        if ":" not in kv:
            gs.fatal(f"weights entry '{kv}' is not in key:value form.")
        k, v = kv.split(":", 1)
        k = k.strip().lower()
        if k not in _VALID_COMPONENTS:
            gs.fatal(f"unknown component '{k}'; allowed: "
                     f"{', '.join(_VALID_COMPONENTS)}")
        try:
            w[k] = float(v)
        except ValueError:
            gs.fatal(f"weights entry '{kv}' has non-numeric value.")
    tot = sum(w.values())
    if tot <= 0:
        gs.fatal("All composite-score weights are zero.")
    return {k: v / tot for k, v in w.items()}


def _transform_area(area_km2, kind):
    if area_km2 is None or area_km2 <= 0:
        return 0.0
    if kind == "linear":
        return area_km2
    if kind == "sqrt":
        return math.sqrt(area_km2)
    return math.log(1.0 + area_km2)        # default: log1p


def _normalise(values):
    """Min-max normalise to [0, 1]. Degenerate ranges → constant 0.5."""
    if not values:
        return []
    vmin = min(values)
    vmax = max(values)
    if vmax <= vmin:
        return [0.5] * len(values)
    return [(v - vmin) / (vmax - vmin) for v in values]


def _borda_from_reports(per_region_cands):
    """Compute a Borda-style score per candidate from each region's intra-
    region ranking. The top candidate in a region with K candidates gets K
    points, the second K-1, etc. When MC rank-1 probabilities are available
    and non-zero, they multiply the Borda contribution so weight uncertainty
    survives the cross-rank step.

    Returns {region_id -> {cand_index -> borda_score}}.
    """
    out = {}
    for region_id, cands in per_region_cands.items():
        if not cands:
            out[region_id] = {}
            continue
        n = len(cands)
        # cands are assumed already sorted by intra-region rank (best first).
        scores = {}
        for i, c in enumerate(cands):
            base = float(n - i)
            mc = c.get("rank1_probability")
            try:
                mc = float(mc) if mc is not None else None
            except (TypeError, ValueError):
                mc = None
            scores[i] = base * (mc if (mc is not None and mc > 0) else 1.0)
        out[region_id] = scores
    return out


def _region_id_from_path(path):
    stem = os.path.basename(path).rsplit(".", 1)[0]
    for tag in ("_report", "-report"):
        if stem.endswith(tag):
            stem = stem[:-len(tag)]
    return stem


def main():
    opt_reports   = options["reports"]
    opt_regids    = options["region_ids"]
    opt_weights   = options["weights"]
    opt_area_tx   = options["area_transform"]
    opt_n_top     = int(options["n_top"])
    opt_output    = options["output"]

    report_paths = [p.strip() for p in opt_reports.split(",") if p.strip()]
    if not report_paths:
        gs.fatal("No report files given.")

    if opt_regids:
        region_ids = [r.strip() for r in opt_regids.split(",") if r.strip()]
        if len(region_ids) != len(report_paths):
            gs.fatal(f"region_ids has {len(region_ids)} entries but "
                     f"reports has {len(report_paths)}.")
    else:
        region_ids = [_region_id_from_path(p) for p in report_paths]

    w = _parse_weights(opt_weights)

    # ── Load reports and tag candidates with provenance ──────────────────
    per_region_cands = {}
    merged = []
    for region_id, path in zip(region_ids, report_paths):
        if not os.path.isfile(path):
            gs.warning(f"report not found: {path} — skipping region "
                       f"'{region_id}'.")
            per_region_cands[region_id] = []
            continue
        try:
            with open(path) as f:
                rep = json.load(f)
        except (OSError, ValueError) as e:
            gs.warning(f"could not read {path}: {e} — skipping.")
            per_region_cands[region_id] = []
            continue
        cands = list(rep.get("candidates", []) or [])
        # Sort by intra-region rank (best first). p.rank stores 'rank' as
        # 1..N when present; fall back to suit_mean descending.
        cands.sort(key=lambda c: (c.get("rank", 10**6),
                                  -float(c.get("suit_mean", 0))))
        per_region_cands[region_id] = cands
        for i, c in enumerate(cands):
            merged.append({"region": region_id, "_intra_idx": i, **c})

    if not merged:
        # All regions returned empty candidate arrays (e.g. every site's
        # AOI was excluded by the hard-exclusion mask). Emit a valid
        # empty cross report and exit cleanly — this is a legitimate
        # all-sites-fail outcome, not a pipeline error.
        gs.warning(
            f"No candidates across {len(region_ids)} region(s); "
            "writing empty cross-ranking report.")
        empty = {
            "n_regions":  len(region_ids),
            "n_top":      0,
            "candidates": [],
            "per_region": [
                {"region_id": rid, "report": rp, "n_candidates": 0}
                for rid, rp in zip(region_ids, report_paths)
            ],
            "note": ("No candidates ingested from any region report. "
                     "All AOIs were excluded by hard-exclusion masks."),
        }
        with open(opt_output, "w") as f:
            json.dump(empty, f, indent=2)
        gs.message(f"Cross-ranking report written to: {opt_output}")
        return 0

    # ── Compute the three composite-score components ─────────────────────
    borda_map = _borda_from_reports(per_region_cands)

    raw_suit  = [float(c.get("suit_mean", 0.0))  for c in merged]
    raw_area  = [_transform_area(c.get("area_km2"), opt_area_tx)
                 for c in merged]
    raw_borda = [borda_map.get(c["region"], {}).get(c["_intra_idx"], 0.0)
                 for c in merged]

    n_suit  = _normalise(raw_suit)
    n_area  = _normalise(raw_area)
    n_borda = _normalise(raw_borda)

    # ── Composite score and global sort ──────────────────────────────────
    for c, s, a, b in zip(merged, n_suit, n_area, n_borda):
        c["_suit_norm"]  = round(s, 6)
        c["_area_norm"]  = round(a, 6)
        c["_borda_norm"] = round(b, 6)
        c["composite_score"] = round(
            w["suit"] * s + w["area"] * a + w["borda"] * b, 6)

    merged.sort(key=lambda c: c["composite_score"], reverse=True)
    top = merged[:opt_n_top]

    # ── Reassemble for output: clean private fields, add cross_rank ──────
    clean = []
    for new_rank, c in enumerate(top, 1):
        out = {
            "cross_rank":      new_rank,
            "region":          c["region"],
            "intra_region_rank": c.get("rank", c["_intra_idx"] + 1),
            "composite_score": c["composite_score"],
            "suit_mean":       c.get("suit_mean"),
            "suit_std":        c.get("suit_std"),
            "area_km2":        c.get("area_km2"),
            "rank1_probability": c.get("rank1_probability"),
            "components": {
                "suit_norm":  c["_suit_norm"],
                "area_norm":  c["_area_norm"],
                "borda_norm": c["_borda_norm"],
            },
        }
        clean.append(out)

    summary = {
        "schema":        "p.rank.cross/v1",
        "n_regions":     len(region_ids),
        "n_candidates":  len(merged),
        "n_returned":    len(clean),
        "weights":       w,
        "area_transform": opt_area_tx,
        "regions": [
            {"region_id": rid, "report": rp,
             "n_candidates": len(per_region_cands.get(rid, []))}
            for rid, rp in zip(region_ids, report_paths)
        ],
        "candidates": clean,
    }

    with open(opt_output, "w") as f:
        json.dump(summary, f, indent=2)

    gs.message(f"Cross-ranked {len(merged)} candidates across "
               f"{len(region_ids)} regions; top {len(clean)} written to "
               f"{opt_output}.")
    gs.message(f"Top {min(5, len(clean))}:")
    for c in clean[:5]:
        gs.message(f"  #{c['cross_rank']} region={c['region']}  "
                   f"composite={c['composite_score']:.3f}  "
                   f"suit_mean={c['suit_mean']}  area_km2={c['area_km2']}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
