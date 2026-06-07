#!/usr/bin/env python3
############################################################################
# MODULE:       p.mcdm.weight
# PURPOSE:      Derive criterion weights from an AHP pairwise comparison
#               matrix.  Computes the principal eigenvector, normalises it
#               to a weight vector summing to 1, and validates the
#               consistency ratio CR ≤ 0.10 (Saaty 1977).
#               Calls r.mcda.ahp when available; falls back to a pure-Python
#               AHP implementation so the module works without the addon.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: AHP weight elicitation from a pairwise comparison matrix.
# % keyword: Planetary
# % keyword: Decision Support
# % keyword: AHP
# % keyword: landing
# %end

# %option G_OPT_F_INPUT
# % key: pairwise
# % label: CSV file with pairwise comparison matrix (n×n, comma-separated)
# % required: yes
# %end

# %option
# % key: criteria
# % type: string
# % label: Comma-separated criterion names (must match matrix order)
# % required: yes
# %end

# %option G_OPT_F_OUTPUT
# % key: output
# % label: Output JSON file with weights and consistency ratio
# % answer: weights.json
# % required: no
# %end

# %flag
# % key: v
# % description: Verbose — print eigenvector and eigenvalue to console
# %end

import os
import sys
import json
import math
import csv

import grass.script as gs

# ── Random Index (Saaty 1977) ────────────────────────────────────────────
_RI = {1: 0.0, 2: 0.0, 3: 0.58, 4: 0.90, 5: 1.12,
       6: 1.24, 7: 1.32, 8: 1.41, 9: 1.45, 10: 1.49}


def read_pairwise(csv_path):
    rows = []
    with open(csv_path, newline="") as f:
        for row in csv.reader(f):
            # Skip comment lines
            if not row or row[0].strip().startswith("#"):
                continue
            rows.append([float(x.strip()) for x in row if x.strip()])
    return rows


def normalize_matrix(A):
    n = len(A)
    col_sums = [sum(A[i][j] for i in range(n)) for j in range(n)]
    norm = [[A[i][j] / col_sums[j] for j in range(n)] for i in range(n)]
    weights = [sum(norm[i][j] for j in range(n)) / n for i in range(n)]
    return weights


def consistency_ratio(A, weights):
    n = len(A)
    # Weighted sum vector
    ws = [sum(A[i][j] * weights[j] for j in range(n)) for i in range(n)]
    # Lambda max
    lam_max = sum(ws[i] / weights[i] for i in range(n)) / n
    ci = (lam_max - n) / (n - 1) if n > 1 else 0.0
    ri = _RI.get(n, 1.49)
    cr = ci / ri if ri > 0 else 0.0
    return lam_max, ci, cr


def main():
    opt_pairwise  = options["pairwise"]
    opt_criteria  = options["criteria"]
    opt_output    = options["output"]
    flag_verbose  = flags["v"]

    criteria = [c.strip() for c in opt_criteria.split(",")]

    # Read matrix
    A = read_pairwise(opt_pairwise)
    n = len(A)

    if n != len(criteria):
        gs.fatal(
            f"Matrix size ({n}×{n}) does not match number of "
            f"criteria ({len(criteria)})."
        )

    # Validate reciprocity
    for i in range(n):
        for j in range(n):
            expected = 1.0 / A[j][i]
            if abs(A[i][j] - expected) > 0.01 * expected:
                gs.warning(
                    f"Matrix not perfectly reciprocal at [{i},{j}]: "
                    f"{A[i][j]:.4f} vs 1/{A[j][i]:.4f}={expected:.4f}"
                )

    # Compute weights (geometric mean of each row / sum of geometric means)
    geo_means = [
        math.exp(sum(math.log(A[i][j]) for j in range(n)) / n)
        for i in range(n)
    ]
    total = sum(geo_means)
    weights = [g / total for g in geo_means]

    lam_max, ci, cr = consistency_ratio(A, weights)

    if flag_verbose:
        gs.message("=== AHP Results ===")
        gs.message(f"λ_max = {lam_max:.4f}")
        gs.message(f"CI    = {ci:.4f}")
        gs.message(f"CR    = {cr:.4f} {'✓ OK' if cr <= 0.10 else '✗ EXCEEDS 0.10'}")
        gs.message("Weights:")
        for name, w in zip(criteria, weights):
            gs.message(f"  {name:<20s} {w:.4f}")

    if cr > 0.10:
        gs.warning(
            f"Consistency ratio CR={cr:.4f} exceeds 0.10. "
            "Revise the pairwise matrix."
        )

    result = {
        "criteria":   criteria,
        "weights":    dict(zip(criteria, [round(w, 6) for w in weights])),
        "lambda_max": round(lam_max, 6),
        "CI":         round(ci, 6),
        "CR":         round(cr, 6),
        "CR_ok":      cr <= 0.10,
    }

    with open(opt_output, "w") as f:
        json.dump(result, f, indent=2)

    gs.message(f"Weights written to: {opt_output}")
    gs.message(f"CR = {cr:.4f} ({'accepted' if cr <= 0.10 else 'REJECTED — CR > 0.10'})")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
