#!/usr/bin/env python3
"""
MODULE:    p.atcorr
AUTHOR(S): Yann Chemin
PURPOSE:   Generic per-pixel atmospheric correction dispatcher for rocky
           solar-system bodies, from airless (Mercury/Moon) through thin,
           dust-variable (Mars) to globally opaque (Venus). Retrieves a
           per-pixel atmospheric state from specific diagnostic bands in
           the input cube itself (instead of one scene-wide scalar) and
           drives the correction with it.
COPYRIGHT: (C) 2026 by the GRASS Development Team
LICENSE:   GNU GPL >=2
"""

# %module
# % description: Per-pixel, body-aware atmospheric correction: retrieves an atmospheric state from diagnostic bands in the cube itself, then dispatches to a none/thin/thick correction strategy.
# % keyword: imagery
# % keyword: atmospheric correction
# % keyword: planetary
# % keyword: radiative transfer
# %end

# %option G_OPT_I_GROUP
# % key: input
# % required: yes
# % label: Input image group (calibrated reflectance / I/F)
# % description: Band order must match the wavelengths= CSV row order, unless bands carry wavelength_um sidecar metadata (p.in.isis).
# %end

# %option
# % key: body
# % type: string
# % required: yes
# % options: mars,venus,mercury,moon
# % label: Target planetary body
# % description: Selects the regime (none/thin/thick) and diagnostic-band registry from matter_bands.json's body_meta.<body>.atmosphere block.
# %end

# %option G_OPT_R_OUTPUT
# % key: output
# % required: yes
# % label: Prefix for output corrected raster maps
# % description: One map per input band: <prefix>.<bandname>. Venus bands with no registered atmospheric window are written as NULL (no physics applies there).
# %end

# %option
# % key: wavelengths
# % type: string
# % required: no
# % label: Two-column CSV: wavelength_um,fwhm_um — one row per band in the group
# %end

# %option G_OPT_R_INPUT
# % key: incidence
# % required: no
# % label: Incidence-angle raster [degrees] (required for the thin/Mars regime)
# %end

# %option G_OPT_R_INPUT
# % key: emission
# % required: no
# % label: Emission-angle raster [degrees] (required for the thin/Mars regime)
# %end

# %option G_OPT_R_INPUT
# % key: phase
# % required: no
# % label: Phase-angle raster [degrees] (required for the thin/Mars regime)
# %end

# %option
# % key: model
# % type: string
# % required: no
# % options: isotropic1,isotropic2,anisotropic1,anisotropic2
# % answer: isotropic2
# % label: Hapke/Chandrasekhar model passed to p.atcorr.hapke (thin regime only)
# %end

# %option
# % key: tau_bins
# % type: integer
# % required: no
# % answer: 5
# % label: Number of discrete tau values spanning [tau_clear, tau_dusty] (thin regime)
# % description: p.atcorr.hapke is evaluated once per bin per band; each pixel's per-pixel retrieved tau is then linearly interpolated between its two bracketing bins. More bins cost proportionally more p.atcorr.hapke calls.
# %end

# %option
# % key: window_tolerance
# % type: double
# % required: no
# % answer: 0.03
# % label: Max |wavelength - window center| [um] for a band to be treated as a Venus atmospheric window (thick regime)
# %end

# %option
# % key: smooth
# % type: double
# % required: no
# % label: Gaussian smoothing sigma [pixels] applied to the retrieved atmospheric-parameter map before correction
# %end

# %option
# % key: db
# % type: string
# % required: no
# % label: Custom band database JSON file (overrides the built-in matter_bands.json)
# %end

# %flag
# % key: m
# % label: Also write the retrieved atmospheric-parameter map (<output>_param)
# %end

import importlib.util
import os
import sys

import grass.script as gs


def _load_matter_bands_module():
    """Load p.matter.bands as a plain Python module (white-box reuse).

    After dpkg installation both scripts land in the same scripts/ directory,
    so check there first (installed path).  Fall back to the source-tree
    sibling directory so the dev build also works.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        # installed: scripts/p.matter.bands  (no .py extension)
        os.path.join(here, "p.matter.bands"),
        # installed: scripts/p.matter.bands.py (if packaged with extension)
        os.path.join(here, "p.matter.bands.py"),
        # dev source tree: planetary/p.atcorr/../p.matter.bands/p.matter.bands.py
        os.path.normpath(os.path.join(here, "..", "p.matter.bands", "p.matter.bands.py")),
    ]
    for script_path in candidates:
        if os.path.isfile(script_path):
            from importlib.machinery import SourceFileLoader
            loader = SourceFileLoader("pmb_reuse", script_path)
            spec = importlib.util.spec_from_loader("pmb_reuse", loader)
            mod = importlib.util.module_from_spec(spec)
            loader.exec_module(mod)
            return mod
    raise FileNotFoundError(
        "p.matter.bands script not found. Searched:\n" +
        "\n".join(f"  {p}" for p in candidates))


pmb = _load_matter_bands_module()


def _resolve_wavelengths(opt_input, opt_wavelengths):
    band_names = pmb._get_group_bands(opt_input)
    wls = pmb._get_sidecar_wavelengths(band_names)
    if wls is None:
        if not opt_wavelengths:
            gs.fatal(
                "No wavelength_um sidecar metadata on the input bands; "
                "wavelengths= CSV is required.")
        wls, _fwhms = pmb._read_wavelengths_csv(opt_wavelengths)
        if len(wls) != len(band_names):
            gs.fatal(
                "wavelengths= has {} rows but the group has {} bands.".format(
                    len(wls), len(band_names)))
    wl_dict = pmb._build_wl_dict(band_names, wls)
    return band_names, wl_dict


def _atmosphere_meta(db, body):
    meta = db.get("body_meta", {}).get(body, {}).get("atmosphere")
    if meta is None:
        gs.fatal(
            "No body_meta.{}.atmosphere entry in the band database; "
            "p.atcorr does not know this body's regime.".format(body))
    return meta


# ── Regime: none (Mercury, Moon) ────────────────────────────────────────────

def _run_none(band_names, output_prefix):
    gs.message(
        "Regime 'none' (airless body): passing {} bands through unchanged."
        .format(len(band_names)))
    region = gs.region()
    for bn in band_names:
        out = "{}.{}".format(output_prefix, bn.split("@")[0])
        arr = pmb._read_band(bn)
        pmb._write_band(arr, out, region)
    return None


# ── Regime: thin (Mars) ─────────────────────────────────────────────────────

def _retrieval_gas_entry(db, body, gas_name):
    gases = db["bodies"][body]["gases"]
    for g in gases:
        if g["name"] == gas_name:
            return g
    gs.fatal(
        "retrieval_gas '{}' (named in body_meta.{}.atmosphere) not found "
        "in bodies.{}.gases.".format(gas_name, body, body))


def _retrieve_tau_proxy(db, body, atm_meta, wl_dict, smooth_sigma):
    """Per-pixel tau-proxy raster from the body's registered diagnostic gas
    band, via the same continuum-interpolation band-depth formula used by
    p.matter.bands (Clark & Roush 1984) — reused, not reimplemented."""
    import numpy as np

    gas = _retrieval_gas_entry(db, body, atm_meta["retrieval_gas"])
    retrieval = gas.get("retrieval")
    if retrieval is None:
        gs.fatal("Gas '{}' has no 'retrieval' block in the database.".format(
            gas["name"]))
    ab = gas["absorption_bands"][retrieval["feature_index"]]
    wl_c, wl_l, wl_r = ab["center"], ab["left"], ab["right"]

    bn_c, actual_c = pmb._find_nearest_band(wl_dict, wl_c)
    bn_l, actual_l = pmb._find_nearest_band(wl_dict, wl_l)
    bn_r, actual_r = pmb._find_nearest_band(wl_dict, wl_r)
    if bn_c is None or bn_l is None or bn_r is None:
        gs.fatal(
            "Input group has no bands near the {} retrieval feature "
            "({:.3f}/{:.3f}/{:.3f} um) — cannot retrieve a per-pixel tau "
            "proxy.".format(gas["name"], wl_l, wl_c, wl_r))

    r_l, r_c, r_r = pmb._read_band(bn_l), pmb._read_band(bn_c), pmb._read_band(bn_r)
    bd = pmb._band_depth(r_l, r_c, r_r, actual_l, actual_c, actual_r)

    tau_clear, tau_dusty = atm_meta["tau_clear"], atm_meta["tau_dusty"]
    bd_lo, bd_hi = retrieval["valid_band_depth_range"]
    k_ref = retrieval["k_ref"]

    tau = bd * k_ref
    out_of_range = np.isnan(bd) | (bd < bd_lo) | (bd > bd_hi)
    n_fallback = int(np.sum(out_of_range))
    # tau_clear is the *fallback* for invalid retrievals only; a genuinely
    # retrieved value below tau_clear is a real (clear-ish) pixel and must
    # not be floored back up to tau_clear, or the per-pixel signal is
    # silently discarded whenever k_ref * band_depth happens to undershoot
    # tau_clear -- exactly the failure mode this comment exists to prevent.
    tau = np.where(out_of_range, tau_clear, tau)
    tau = np.clip(tau, 0.0, tau_dusty)

    if n_fallback:
        gs.verbose(
            "{}/{} pixels outside the valid band-depth range "
            "[{}, {}]; filled with tau_clear={}.".format(
                n_fallback, tau.size, bd_lo, bd_hi, tau_clear))

    if smooth_sigma:
        tau = _gaussian_smooth(tau, smooth_sigma)

    return tau


def _gaussian_smooth(arr, sigma_px):
    """Separable Gaussian filter, NaN-aware (edge-replication + null
    exclusion), mirroring the retrieval-noise smoothing pattern used by
    image-based AOD/H2O retrieval tools (e.g. i.hyper.atcorr's smooth=)."""
    import numpy as np
    try:
        from scipy.ndimage import gaussian_filter
    except ImportError:
        gs.warning("scipy not available; smooth= ignored.")
        return arr
    nanmask = np.isnan(arr)
    filled = np.where(nanmask, np.nanmean(arr), arr)
    smoothed = gaussian_filter(filled, sigma=sigma_px, mode="nearest")
    return np.where(nanmask, float("nan"), smoothed)


def _hapke_correct_band(band_name, tau_value, wha, opts):
    """One p.atcorr.hapke call at a fixed scalar tau; returns the corrected
    raster name. Thin wrapper around p.matter.bands._atcorr_band so the
    validated Hapke/Chandrasekhar engine is reused unmodified."""
    params = {
        "model": opts["model"],
        "incidence": opts["incidence"],
        "emission": opts["emission"],
        "phase": opts["phase"],
        "tau": tau_value,
        "wha": wha,
    }
    tmp_prefix = "p_atcorr_tmp_{}".format(os.getpid())
    return pmb._atcorr_band(band_name, "{}_{:.4f}".format(tmp_prefix, tau_value),
                             params)


def _run_thin(db, body, band_names, wl_dict, output_prefix, opts, write_param):
    import numpy as np

    atm_meta = _atmosphere_meta_checked(db, body)
    if not (opts["incidence"] and opts["emission"] and opts["phase"]):
        gs.fatal(
            "Regime 'thin' (body={}) requires incidence=, emission= and "
            "phase= (p.phocube output) to drive p.atcorr.hapke.".format(body))

    tau_proxy = _retrieve_tau_proxy(db, body, atm_meta, wl_dict, opts["smooth"])
    region = gs.region()

    if write_param:
        pmb._write_band(tau_proxy, "{}_param".format(output_prefix), region)

    n_bins = max(2, opts["tau_bins"])
    bins = np.linspace(atm_meta["tau_clear"], atm_meta["tau_dusty"], n_bins)
    wha = atm_meta["wha"]

    # The retrieval can genuinely read below tau_clear or above tau_dusty
    # for individual pixels (real noise, or a genuinely clearer/dustier
    # pixel than the body's nominal bracket) -- the tau-bin table only
    # spans [tau_clear, tau_dusty], so clamp a *correction-only* copy to
    # that domain before bracketing. The diagnostic tau_proxy map written
    # above keeps the true, unclamped per-pixel retrieval; only the value
    # used to pick/interpolate Hapke bins is clamped here. (Regression:
    # an earlier version had no separate clamp and left every pixel
    # outside [tau_clear, tau_dusty] as NULL in the corrected output --
    # caught by running this on real CRISM data, where most retrieved
    # values fell below tau_clear.)
    tau_for_bins = np.clip(tau_proxy, bins[0], bins[-1])

    gs.message(
        "Regime 'thin': {} tau bins x {} bands = {} p.atcorr.hapke calls."
        .format(n_bins, len(band_names), n_bins * len(band_names)))

    for bn in band_names:
        bin_arrays = []
        for tau_value in bins:
            corr_name = _hapke_correct_band(bn, float(tau_value), wha, opts)
            bin_arrays.append(pmb._read_band(corr_name))

        out_arr = np.empty_like(tau_for_bins)
        out_arr[:] = float("nan")
        for i in range(n_bins - 1):
            lo, hi = bins[i], bins[i + 1]
            mask = (tau_for_bins >= lo) & (tau_for_bins <= hi)
            if not np.any(mask):
                continue
            t = (tau_for_bins[mask] - lo) / (hi - lo)
            out_arr[mask] = (bin_arrays[i][mask] * (1.0 - t)
                             + bin_arrays[i + 1][mask] * t)

        out_name = "{}.{}".format(output_prefix, bn.split("@")[0])
        pmb._write_band(out_arr, out_name, region)

    return tau_proxy


def _atmosphere_meta_checked(db, body):
    atm_meta = _atmosphere_meta(db, body)
    for key in ("tau_clear", "tau_dusty", "wha", "retrieval_gas"):
        if key not in atm_meta:
            gs.fatal(
                "body_meta.{}.atmosphere is missing required key '{}' for "
                "the thin regime.".format(body, key))
    return atm_meta


# ── Regime: thick (Venus) ───────────────────────────────────────────────────

def _run_thick(db, body, band_names, wl_dict, output_prefix, tolerance, write_param):
    import numpy as np

    atm_meta = _atmosphere_meta(db, body)
    windows = atm_meta.get("atmosphere_windows", [])
    region = gs.region()

    for bn in band_names:
        wl = wl_dict[bn]
        match = min(windows, key=lambda w: abs(w["center_um"] - wl),
                    default=None)
        if match is None or abs(match["center_um"] - wl) > tolerance:
            gs.warning(
                "Band {} ({:.4f} um) is not a registered atmospheric window "
                "for {} (cloud-top tau ~25-40 makes surface retrieval "
                "physically impossible there); writing NULL.".format(
                    bn, wl, body))
            out_arr = np.full(
                (int(region["rows"]), int(region["cols"])), float("nan"))
            pmb._write_band(out_arr, "{}.{}".format(
                output_prefix, bn.split("@")[0]), region)
            continue

        ref_bn, _actual = pmb._find_nearest_band(wl_dict, match["reference_um"],
                                                   tolerance_um=tolerance)
        if ref_bn is None:
            gs.warning(
                "Window band {} has no reference band near {:.4f} um in "
                "this group; writing NULL.".format(bn, match["reference_um"]))
            out_arr = np.full(
                (int(region["rows"]), int(region["cols"])), float("nan"))
            pmb._write_band(out_arr, "{}.{}".format(
                output_prefix, bn.split("@")[0]), region)
            continue

        window_arr = pmb._read_band(bn)
        ref_arr = pmb._read_band(ref_bn)
        with np.errstate(invalid="ignore", divide="ignore"):
            ratio = window_arr / ref_arr
        scene_median = np.nanmedian(ratio)

        gs.verbose(
            "Window {:.4f} um / reference {:.4f} um: scene-median ratio = "
            "{:.4f} (per-pixel cloud-opacity normalisation, {} instrument "
            "window).".format(match["center_um"], match["reference_um"],
                               scene_median, match.get("instrument", "?")))

        with np.errstate(invalid="ignore", divide="ignore"):
            corrected = window_arr * (scene_median / ratio)

        out_name = "{}.{}".format(output_prefix, bn.split("@")[0])
        pmb._write_band(corrected, out_name, region)

        if write_param:
            pmb._write_band(ratio, "{}_param_{}".format(
                output_prefix, bn.split("@")[0]), region)


def main():
    options, flags = gs.parser()

    opt_input = options["input"]
    body = options["body"]
    output_prefix = options["output"]
    opt_wavelengths = options["wavelengths"]
    db_path = options["db"] or None
    write_param = flags["m"]

    db = pmb._load_database(db_path)
    atm_meta = _atmosphere_meta(db, body)
    regime = atm_meta["regime"]

    band_names, wl_dict = _resolve_wavelengths(opt_input, opt_wavelengths)

    if regime == "none":
        _run_none(band_names, output_prefix)
    elif regime == "thin":
        opts = {
            "incidence": options["incidence"],
            "emission": options["emission"],
            "phase": options["phase"],
            "model": options["model"],
            "tau_bins": int(options["tau_bins"]),
            "smooth": float(options["smooth"]) if options["smooth"] else None,
        }
        _run_thin(db, body, band_names, wl_dict, output_prefix, opts, write_param)
    elif regime == "thick":
        tolerance = float(options["window_tolerance"])
        _run_thick(db, body, band_names, wl_dict, output_prefix, tolerance,
                   write_param)
    else:
        gs.fatal("Unknown regime '{}' for body '{}'.".format(regime, body))

    gs.message("Done. Corrected bands written with prefix '{}'.".format(
        output_prefix))


if __name__ == "__main__":
    main()
