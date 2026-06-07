#!/usr/bin/env python3
"""
train_p3.py — offline training script for the p.crater.draw P3 shallow-ML
               rescoring model.

Usage (two modes):

  # Mode A: read directly from a GRASS vector (runs v.db.select internally)
  python3 train_p3.py --map=detected_rims --output=p3_model.bin

  # Mode B: read a CSV produced by v.db.select -c
  #   v.db.select map=detected_rims col=confidence,method flags=c > craters.csv
  python3 train_p3.py --csv=craters.csv --output=p3_model.bin

The script labels rows from a p.crater.draw method=both run:
  positive: method=merged  AND confidence >= pos_threshold (default 0.85)
  negative: method=dem|image AND confidence <= neg_threshold (default 0.60)

Features (mirror of detect_ml.c::make_features):
  f[0] = cP1           (DEM confidence; 0 for image-only candidates)
  f[1] = cP2           (image confidence; 0 for dem-only candidates)
  f[2] = cP1^2
  f[3] = cP2^2
  f[4] = |cP1 - cP2|
  f[5] = (cP1 + cP2) / 2

For method=merged candidates cP1 = cP2 = confidence is used as an
approximation (the merged score is an NMS-selected mix of both detectors).
For better per-detector separation, run p.crater.draw twice with method=dem
and method=image separately, join spatially, and feed the two confidence
values explicitly via --dem_csv and --image_csv (see below).

Output binary format (read by detect_ml.c::ml_load_model):
  char[4]   "PCDM"
  int32     version=1
  float32   w[6]
  float32   bias

Author:  Yann Chemin <dr.yann.chemin@gmail.com>
License: The Unlicense (https://unlicense.org)
"""

import argparse
import math
import struct
import sys


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--map",  help="GRASS vector map from a method=both run")
    p.add_argument("--csv",  help="CSV from v.db.select (columns: confidence,method)")
    p.add_argument("--dem_csv",
                   help="Optional CSV with DEM-only run (columns: cx,cy,confidence); "
                        "overrides merged-approximation for cP1")
    p.add_argument("--image_csv",
                   help="Optional CSV with image-only run (columns: cx,cy,confidence); "
                        "overrides merged-approximation for cP2")
    p.add_argument("--output", default="p3_model.bin",
                   help="Output binary model file (default: p3_model.bin)")
    p.add_argument("--pos_threshold", type=float, default=0.85,
                   help="Confidence floor for positive labels (merged, default 0.85)")
    p.add_argument("--neg_threshold", type=float, default=0.60,
                   help="Confidence ceiling for negative labels (singleton, default 0.60)")
    p.add_argument("--lr", type=float, default=0.1,
                   help="SGD learning rate (default 0.1)")
    p.add_argument("--epochs", type=int, default=200,
                   help="Training epochs (default 200)")
    p.add_argument("--l2", type=float, default=1e-4,
                   help="L2 regularisation coefficient (default 1e-4)")
    return p.parse_args()


def _sigmoid(x):
    if x >= 0:
        e = math.exp(-x)
        return 1.0 / (1.0 + e)
    e = math.exp(x)
    return e / (1.0 + e)


def features(cP1, cP2):
    return [cP1, cP2, cP1*cP1, cP2*cP2, abs(cP1-cP2), 0.5*(cP1+cP2)]


def load_grass_csv(map_name):
    """Call v.db.select inside GRASS and return list of (confidence, method) rows."""
    try:
        import grass.script as gs
    except ImportError:
        sys.exit("--map mode requires running inside a GRASS session (grass.script not available).")
    raw = gs.read_command("v.db.select", map=map_name,
                          columns="confidence,method", flags="c")
    rows = []
    for line in raw.strip().splitlines():
        parts = line.split("|")
        if len(parts) < 2:
            continue
        try:
            rows.append((float(parts[0]), parts[1].strip()))
        except ValueError:
            pass
    return rows


def load_csv(path):
    """Read a two-column CSV (confidence|method) produced by v.db.select -c."""
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split("|")
            if len(parts) < 2:
                continue
            try:
                rows.append((float(parts[0]), parts[1].strip()))
            except ValueError:
                pass
    return rows


def load_xy_csv(path):
    """Load cx,cy,confidence from a three-column CSV (optional per-detector files)."""
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split("|")
            if len(parts) < 3:
                continue
            try:
                rows.append((float(parts[0]), float(parts[1]), float(parts[2])))
            except ValueError:
                pass
    return rows


def build_dataset(rows, pos_thr, neg_thr):
    """
    Convert (confidence, method) rows to (X, y) arrays.
    Returns (X: list of 6-float vectors, y: list of 0/1 labels, n_pos, n_neg).
    """
    X, y = [], []
    n_pos = n_neg = 0
    for conf, method in rows:
        if method == "merged" and conf >= pos_thr:
            cP1 = cP2 = conf   # approximation: both detectors agreed
            X.append(features(cP1, cP2))
            y.append(1)
            n_pos += 1
        elif method in ("dem", "image") and conf <= neg_thr:
            cP1 = conf if method == "dem"   else 0.0
            cP2 = conf if method == "image" else 0.0
            X.append(features(cP1, cP2))
            y.append(0)
            n_neg += 1
    return X, y, n_pos, n_neg


def train_logistic(X, y, lr, epochs, l2):
    """
    Batch-gradient-descent logistic regression.
    No external dependencies — pure Python.
    Returns (weights: list[6], bias: float).
    """
    NFEAT = 6
    w = [0.0] * NFEAT
    b = 0.0
    n = len(y)
    if n == 0:
        return w, b

    for epoch in range(epochs):
        dw = [0.0] * NFEAT
        db = 0.0
        loss = 0.0
        for xi, yi in zip(X, y):
            z = b + sum(w[k] * xi[k] for k in range(NFEAT))
            p = _sigmoid(z)
            err = p - yi
            loss += -(yi * math.log(p + 1e-12) + (1-yi) * math.log(1 - p + 1e-12))
            for k in range(NFEAT):
                dw[k] += err * xi[k]
            db += err
        # L2 on weights (not bias)
        for k in range(NFEAT):
            dw[k] = dw[k] / n + l2 * w[k]
        db /= n
        for k in range(NFEAT):
            w[k] -= lr * dw[k]
        b -= lr * db

        if (epoch + 1) % 50 == 0:
            loss /= n
            reg  = 0.5 * l2 * sum(wi*wi for wi in w)
            print(f"  epoch {epoch+1:4d}/{epochs}  loss={loss+reg:.4f}")

    return w, b


def write_model(path, w, b):
    """Write PCDM binary model file (read by detect_ml.c::ml_load_model)."""
    with open(path, "wb") as f:
        f.write(b"PCDM")
        f.write(struct.pack("<i", 1))          # version
        f.write(struct.pack("<6f", *w))
        f.write(struct.pack("<f", b))
    print(f"Model written to '{path}'")
    print(f"  weights: {[f'{wi:.6f}' for wi in w]}")
    print(f"  bias:    {b:.6f}")


def main():
    args = parse_args()

    # Load main dataset
    if args.map:
        rows = load_grass_csv(args.map)
        print(f"Loaded {len(rows)} candidates from GRASS vector '{args.map}'")
    elif args.csv:
        rows = load_csv(args.csv)
        print(f"Loaded {len(rows)} candidates from CSV '{args.csv}'")
    else:
        sys.exit("Provide --map or --csv.")

    X, y, n_pos, n_neg = build_dataset(rows, args.pos_threshold, args.neg_threshold)
    print(f"Training set: {n_pos} positives, {n_neg} negatives")
    if n_pos == 0 or n_neg == 0:
        print("WARNING: one class is empty — adjust pos_threshold / neg_threshold.")
        if n_pos == 0 and n_neg == 0:
            sys.exit("No training samples found. Aborting.")

    print(f"Training logistic regression: {args.epochs} epochs, lr={args.lr}, l2={args.l2}")
    w, b = train_logistic(X, y, lr=args.lr, epochs=args.epochs, l2=args.l2)

    # Quick accuracy report on the training set
    correct = sum(1 for xi, yi in zip(X, y)
                  if (1 if _sigmoid(b + sum(w[k]*xi[k] for k in range(6))) >= 0.5 else 0) == yi)
    print(f"Training accuracy: {correct}/{len(y)} ({100*correct/len(y):.1f}%)")

    write_model(args.output, w, b)


if __name__ == "__main__":
    main()
