#!/usr/bin/env python3
"""
MODULE:       p.change
AUTHOR:       Yann Chemin
PURPOSE:      Per-pixel temporal change between two rasters.

              Computes: difference (b-a), ratio (b/a),
                        normalised difference (b-a)/(b+a),
                        or log-ratio ln(b/a).

              Optional absolute or relative significance threshold:
              pixels below threshold are set to NODATA.
              Optional binary change-mask output (-m flag).

LICENSE:      The Unlicense - public domain
"""

# %module
# % description: Per-pixel temporal change between two rasters (difference, ratio, NDVI-style, log-ratio).
# % keyword: raster
# % keyword: change detection
# % keyword: temporal
# % keyword: difference
# % keyword: ratio
# % keyword: planetary
# %end

# %option G_OPT_R_INPUT
# % key: input_a
# % label: Earlier (reference) raster
# % description: Raster at time T1 (before event)
# % required: yes
# %end

# %option G_OPT_R_INPUT
# % key: input_b
# % label: Later raster
# % description: Raster at time T2 (after event)
# % required: yes
# %end

# %option G_OPT_R_OUTPUT
# % key: output
# % description: Output change raster
# % required: yes
# %end

# %option
# % key: mode
# % type: string
# % description: Change metric
# % options: difference,ratio,ndiff,log_ratio
# % descriptions: difference;b − a (additive change, reflectance/elevation units);ratio;b / a (multiplicative change);ndiff;(b − a) / (b + a) normalised difference, range −1..+1;log_ratio;ln(b / a) for multiplicative / log-normal processes
# % answer: difference
# % required: no
# %end

# %option
# % key: threshold
# % type: double
# % description: Absolute significance threshold — pixels where |change| < threshold are set to NULL
# % required: no
# %end

# %option
# % key: rel_threshold
# % type: double
# % description: Relative significance threshold — pixels where |change / a| < rel_threshold are set to NULL (ratio of the change to the reference value)
# % required: no
# %end

# %flag
# % key: m
# % description: Output a binary change mask (1 = significant change, 0 = no change) instead of the metric value
# %end

# %flag
# % key: s
# % description: Subtract the spatial mean of the change image before thresholding (removes scene-wide bias; useful when comparing images with slight calibration offsets)
# %end

import grass.script as gs


def main():
    options, flags = gs.parser()

    a      = options["input_a"]
    b      = options["input_b"]
    out    = options["output"]
    mode   = options["mode"]
    thr    = options["threshold"]
    rel    = options["rel_threshold"]
    do_mask   = flags["m"]
    do_demean = flags["s"]

    tmp = f"_pchange_tmp_{gs.tempname(8)}"

    try:
        # ── compute raw change metric ────────────────────────────────────────
        if mode == "difference":
            expr = f"{tmp} = {b} - {a}"
        elif mode == "ratio":
            expr = f"{tmp} = if({a} != 0, float({b}) / float({a}), null())"
        elif mode == "ndiff":
            expr = (f"{tmp} = if(({a}) + ({b}) != 0, "
                    f"float({b} - {a}) / float({b} + {a}), null())")
        elif mode == "log_ratio":
            expr = (f"{tmp} = if({a} > 0 && {b} > 0, "
                    f"log({b}) - log({a}), null())")
        else:
            gs.fatal(f"Unknown mode: {mode}")

        gs.run_command("r.mapcalc", expression=expr, overwrite=True)

        # ── optional spatial de-meaning ──────────────────────────────────────
        if do_demean:
            stats = gs.parse_command("r.univar", map=tmp, flags="g", quiet=True)
            mean_val = float(stats.get("mean", 0))
            gs.verbose(f"De-meaning: subtracting mean={mean_val:.6f}")
            expr_dm = f"{tmp} = {tmp} - {mean_val}"
            gs.run_command("r.mapcalc", expression=expr_dm, overwrite=True)

        # ── apply absolute threshold ─────────────────────────────────────────
        if thr:
            thr_f = float(thr)
            expr_thr = f"{tmp} = if(abs({tmp}) >= {thr_f}, {tmp}, null())"
            gs.run_command("r.mapcalc", expression=expr_thr, overwrite=True)

        # ── apply relative threshold ─────────────────────────────────────────
        if rel:
            rel_f = float(rel)
            expr_rel = (f"{tmp} = if({a} != 0 && "
                        f"abs({tmp}) / abs({a}) >= {rel_f}, {tmp}, null())")
            gs.run_command("r.mapcalc", expression=expr_rel, overwrite=True)

        # ── binary mask or continuous output ─────────────────────────────────
        if do_mask:
            gs.run_command("r.mapcalc",
                           expression=f"{out} = if(isnull({tmp}), 0, 1)",
                           overwrite=True)
            gs.run_command("r.colors", map=out, color="grey.eq", quiet=True)
        else:
            gs.run_command("g.rename", raster=f"{tmp},{out}", overwrite=True)
            tmp = None  # renamed, don't delete

        # ── colourisation (continuous output) ────────────────────────────────
        if not do_mask:
            if mode in ("difference", "log_ratio"):
                gs.run_command("r.colors", map=out, color="differences",
                               quiet=True)
            elif mode == "ndiff":
                gs.run_command("r.colors", map=out, color="ndvi",
                               quiet=True)
            # ratio: leave default (or let user apply)

        # ── summary statistics ───────────────────────────────────────────────
        stats = gs.parse_command("r.univar", map=out, flags="g", quiet=True)
        n     = int(stats.get("n", 0))
        mean  = float(stats.get("mean", float("nan")))
        std   = float(stats.get("stddev", float("nan")))
        vmin  = float(stats.get("min", float("nan")))
        vmax  = float(stats.get("max", float("nan")))
        gs.message(f"Change map ({mode}): n={n} valid pixels, "
                   f"mean={mean:.4f}, std={std:.4f}, "
                   f"min={vmin:.4f}, max={vmax:.4f}")

        # ── write map history ────────────────────────────────────────────────
        gs.run_command("r.support", map=out,
                       title=f"p.change {mode}: {b} - {a}",
                       description=f"mode={mode}"
                                   + (f" threshold={thr}" if thr else "")
                                   + (f" rel_threshold={rel}" if rel else ""),
                       source1=a, source2=b,
                       quiet=True)

    finally:
        if tmp:
            gs.run_command("g.remove", type="raster", name=tmp,
                           flags="f", quiet=True)


if __name__ == "__main__":
    main()
