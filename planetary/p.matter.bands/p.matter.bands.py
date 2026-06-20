#!/usr/bin/env python3
"""
MODULE:    p.matter.bands
AUTHOR(S): Yann Chemin
PURPOSE:   Detect and map planetary matter (minerals, ices, gases, organics,
           liquids) by absorption-band depth from a body-aware spectral database
           covering UV–LWIR (0.1–100 µm).
COPYRIGHT: (C) 2026 by the GRASS Development Team
LICENSE:   GNU GPL >=2
"""

# %module
# % description: Detect planetary matter (minerals, ices, gases, organics, liquids) by absorption-band depth from a body-aware spectral database (UV–LWIR).
# % keyword: imagery
# % keyword: spectral
# % keyword: hyperspectral
# % keyword: minerals
# % keyword: planetary
# % keyword: absorption
# %end

# %option G_OPT_I_GROUP
# % key: group
# % required: yes
# % label: Input image group (calibrated reflectance / I/F)
# % description: Image group produced by p.in.pds3/p.in.pds4/p.in.isis. Band order must match the wavelengths= CSV row order.
# %end

# %option
# % key: body
# % type: string
# % required: yes
# % label: Target planetary body
# % description: Controls which matter types and absorption features are loaded from the database.
# % options: mars,moon,mercury,venus,titan,europa,io,enceladus,ceres,asteroid_c_type,asteroid_s_type,asteroid_d_type,comet,pluto,ganymede,callisto,triton,ariel,uranus_moon,generic
# % descriptions: mars;Mars surface and atmosphere;moon;Earth's Moon;mercury;Mercury (MESSENGER/BepiColombo);venus;Venus (surface NIR windows + atmosphere);titan;Saturn's Titan (VIMS atmospheric windows);europa;Jupiter's Europa (irradiated icy surface);io;Jupiter's Io (SO2 frost and sulfur);enceladus;Saturn's Enceladus (clean H2O ice + plume);ceres;Dwarf planet Ceres (ammoniated clays);asteroid_c_type;Carbonaceous (C-type) asteroid (Ryugu/Bennu);asteroid_s_type;Stony (S-type) asteroid;asteroid_d_type;Dark D-type asteroid / Jupiter Trojan (Lucy targets);comet;Cometary nucleus and coma;pluto;Pluto / KBO (volatile ices);ganymede;Jupiter's Ganymede (JUICE/MAJIS 2031);callisto;Jupiter's Callisto (JUICE/MAJIS 2031);triton;Neptune's Triton (future orbiter);ariel;Uranus moon Ariel — strong CO2 ice;uranus_moon;Generic Uranus icy moon (Titania/Oberon/Umbriel);generic;Generic body — load all database entries
# %end

# %option G_OPT_R_OUTPUT
# % key: output_prefix
# % required: yes
# % label: Prefix for output band-depth raster maps
# % description: One map per detected species: <prefix>_<species_name>. Values are band depth [0, 1]. NULL where data is absent or BD < min_bd.
# %end

# %option
# % key: wavelengths
# % type: string
# % required: no
# % label: Two-column CSV: wavelength_um,fwhm_um — one row per band in the group
# % description: Band centre wavelengths and FWHM in micrometres, ordered to match the group band order (same convention as p.mineral.indices). Required unless the bands carry 'wavelength_um' metadata (set by p.in.isis).
# %end

# %option
# % key: matter
# % type: string
# % required: no
# % multiple: yes
# % options: all,minerals,ices,gases,organics,liquids
# % answer: all
# % label: Matter type(s) to detect
# %end

# %option
# % key: db
# % type: string
# % required: no
# % label: Custom band database JSON file
# % description: Overrides the built-in $GISBASE/etc/planetary/matter_bands.json. Useful for adding unreleased species or mission-specific calibration data.
# %end

# %option
# % key: mode
# % type: string
# % required: no
# % options: reflectance,emissivity
# % answer: reflectance
# % label: Input data type
# % description: reflectance — standard I/F or reflectance factor (UV–SWIR sensors); emissivity — thermal emissivity derived from calibrated MIR/TIR radiance via Planck inversion (TES, THEMIS, MERTIS, MIRI). The band-depth formula BD = 1 − signal(λ)/continuum(λ) is identical in both modes; this flag tags output maps and skips non-applicable database species.
# %end

# %option
# % key: min_bd
# % type: double
# % required: no
# % answer: 0.01
# % label: Minimum band depth threshold [0.0–1.0]
# % description: Output pixels with BD below this value are set to NULL (no detection).
# %end

# %option G_OPT_I_GROUP
# % key: endmembers
# % required: no
# % label: Endmember library group for NNLS unmixing (-u)
# % description: Image group containing one pure-spectrum band per endmember. Must have the same band count as the input group (same wavelength order). Output: one abundance map [0,1] per endmember band name.
# %end

# %option
# % key: temperature
# % type: double
# % required: no
# % label: Scene temperature in Kelvin — shifts ice band centers before matching
# % description: Corrects temperature-dependent wavelength shifts in volatile ice bands (H2O, N2, CH4, CO, CO2). Critical for Europa (80–130 K), Enceladus (80 K), Pluto (40 K), Triton (36 K). Refs: Mastrapa et al. 2008; Quirico & Schmitt 1997.
# %end

# %option
# % key: space_weathering
# % type: double
# % required: no
# % answer: 0.0
# % label: Space weathering Is/FeO factor [0.0 = disabled]
# % description: NPFe nanophase iron correction: BD_corr = BD / (1 − α × Is/FeO). Body-specific α from database (Moon 0.40, Mercury 0.35, S-type 0.30). Hapke 2001; Clark et al. 2002.
# %end

# %option
# % key: min_abund
# % type: double
# % required: no
# % answer: 0.01
# % label: Minimum endmember abundance threshold for NNLS output [0.0–1.0]
# %end

# %option G_OPT_R_INPUT
# % key: atcorr_incidence
# % required: no
# % label: Per-pixel incidence angle map (°) from p.phocube [--atcorr]
# %end

# %option G_OPT_R_INPUT
# % key: atcorr_emission
# % required: no
# % label: Per-pixel emission angle map (°) from p.phocube [--atcorr]
# %end

# %option G_OPT_R_INPUT
# % key: atcorr_phase
# % required: no
# % label: Per-pixel phase angle map (°) from p.phocube [--atcorr]
# %end

# %option
# % key: atcorr_model
# % type: string
# % required: no
# % options: Isotropic1,Isotropic2,Anisotropic1,Anisotropic2
# % answer: Isotropic2
# % label: Hapke scattering model for atmospheric correction [--atcorr]
# %end

# %option
# % key: atcorr_tau
# % type: double
# % required: no
# % label: Atmospheric optical depth τ [--atcorr]
# %end

# %option
# % key: atcorr_wha
# % type: double
# % required: no
# % label: Hapke single-scattering albedo wha [--atcorr]
# %end

# %option
# % key: min_conf
# % type: double
# % required: no
# % answer: 0.0
# % label: Minimum band-concordance confidence [0.0–1.0, default 0.0]
# % description: Species whose matched-band fraction (n_bands_in_sensor / n_diagnostic_bands) falls below this value are suppressed. E.g. min_conf=0.5 requires at least half the diagnostic bands to be covered. Use with -q to also write confidence rasters.
# %end

# %option
# % key: report
# % type: string
# % required: no
# % label: Path for JSON detection report
# % description: Write a structured JSON summary of all detections (species, confidence, mean/max BD, n_bands) to this file. Written after all species are processed.
# %end

# %flag
# % key: l
# % description: List detectable species for this body/sensor combination (no raster output)
# %end

# %flag
# % key: q
# % description: Output per-species confidence raster (<prefix>_<species>_conf) — value = fraction of diagnostic bands covered by the sensor [0,1]
# %end

# %flag
# % key: u
# % description: NNLS spectral unmixing mode — output endmember abundance maps instead of band-depth maps (requires endmembers=)
# %end

# %flag
# % key: a
# % description: Apply Hapke atmospheric correction via p.atcorr.hapke before detection (requires atcorr_incidence=, atcorr_emission=, atcorr_phase=, atcorr_tau=, atcorr_wha=)
# %end

# %flag
# % key: c
# % description: Output composite false-colour RGB: R=minerals, G=ices/gases, B=organics (strongest species per type)
# %end

# %flag
# % key: v
# % description: Verbose: report band depth statistics for each species map
# %end

import os
import sys
import json
import tempfile

import grass.script as gs


_NULL = -9999.0

_DB_FILENAME = "matter_bands.json"


def _mapset_misc_path():
    """Return $GISDBASE/$LOCATION/$MAPSET/Misc/ (GRASS mapset misc directory)."""
    env = gs.gisenv()
    return os.path.join(
        env["GISDBASE"], env["LOCATION_NAME"], env["MAPSET"], "Misc"
    )


def _db_search_paths(db_path=None):
    """Ordered list of paths to search for matter_bands.json."""
    if db_path:
        return [db_path]
    return [
        # 1. Mapset Misc/ — written by p_meta on import via p.in.*
        os.path.join(_mapset_misc_path(), _DB_FILENAME),
        # 2. System installation
        os.path.join(os.getenv("GISBASE", ""), "etc", "planetary", _DB_FILENAME),
        # 3. Development tree (module lives two levels below data/)
        os.path.normpath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "..", "..", "data", _DB_FILENAME)),
    ]


# ── Database ──────────────────────────────────────────────────────────────────

def _load_database(db_path=None):
    paths = _db_search_paths(db_path)
    for p in paths:
        p = os.path.normpath(p)
        if os.path.isfile(p):
            with open(p) as f:
                return json.load(f)
    gs.fatal(
        "Band database not found. Searched:\n  {}".format("\n  ".join(paths))
    )


# ── Wavelength resolution ─────────────────────────────────────────────────────

def _read_wavelengths_csv(csv_path):
    """Return (wls_um, fwhms_um) from a two-column wavelength CSV."""
    wls, fwhms = [], []
    with open(csv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            wls.append(float(parts[0]))
            fwhms.append(float(parts[1]) if len(parts) > 1 else 0.001)
    return wls, fwhms


def _get_group_bands(group):
    """Return ordered list of band map names in the image group."""
    out = gs.read_command("i.group", flags="g", group=group).strip()
    return [b.strip() for b in out.splitlines() if b.strip()]


def _get_sidecar_wavelengths(band_names):
    """
    Try to read per-band wavelength_um from GRASS history metadata
    (written by p.in.isis when it imports ISIS BandBin data).

    Returns list of wavelengths (µm) in band order, or None if not available.
    """
    wls = []
    for m in band_names:
        hist = gs.read_command("r.info", flags="h", map=m, quiet=True)
        found = None
        for line in hist.splitlines():
            if "wavelength_um" in line:
                try:
                    found = float(line.split("=")[1].strip())
                except (IndexError, ValueError):
                    pass
                break
        if found is None:
            return None
        wls.append(found)
    return wls


def _build_wl_dict(band_names, wl_list):
    """Build {band_name: wavelength_um} from ordered lists."""
    return dict(zip(band_names, wl_list))


def _find_nearest_band(wl_dict, target_um, tolerance_um=0.030):
    """
    Return (band_name, actual_wavelength) of the band closest to target_um,
    or (None, None) if no band is within tolerance.

    Tolerance scales with wavelength (3 % of target) to accommodate the coarser
    spectral sampling of MIR/FIR instruments (TES ~0.1 µm, PACS ~1 µm).
    """
    tol = max(tolerance_um, 0.03 * target_um)
    best_name, best_wl, best_d = None, None, float("inf")
    for name, wl in wl_dict.items():
        if wl is None:
            continue
        d = abs(wl - target_um)
        if d < best_d:
            best_d, best_name, best_wl = d, name, wl
    return (best_name, best_wl) if best_d <= tol else (None, None)


# ── Phase 4.2 — Temperature correction ───────────────────────────────────────

def _temp_corrected_center(ab, temperature_K):
    """Return temperature-adjusted band center (µm).

    Uses per-band temp_ref_K / temp_shift_um_per_K fields from the database.
    Falls back to the nominal center when no coefficient is stored.
    """
    center = ab["center"]
    if temperature_K is None:
        return center
    t_ref   = ab.get("temp_ref_K")
    t_shift = ab.get("temp_shift_um_per_K")
    if t_ref is None or t_shift is None:
        return center
    return center + t_shift * (temperature_K - t_ref)


# ── Phase 4.3 — Space weathering correction ───────────────────────────────────

def _apply_space_weathering(bd_arr, sw_factor, sw_alpha):
    """Apply NPFe nanophase-iron band-depth correction.

    BD_corr = BD / (1 − alpha × Is/FeO)
    Clips result to [0, 1]. Returns bd_arr unchanged if sw_factor = 0.
    """
    import numpy as np
    if sw_factor <= 0.0 or sw_alpha <= 0.0:
        return bd_arr
    denom = 1.0 - sw_alpha * sw_factor
    if abs(denom) < 1e-6:
        gs.warning("Space weathering denominator near zero — correction skipped.")
        return bd_arr
    return np.clip(bd_arr / denom, 0.0, 1.0)


# ── Phase 4.4 — Atmospheric correction pre-step ───────────────────────────────

def _atcorr_band(band_name, tmp_prefix, atcorr_params):
    """Run p.atcorr.hapke on one band; return corrected raster name."""
    import shutil
    if not shutil.which("p.atcorr.hapke"):
        gs.fatal(
            "p.atcorr.hapke not found in PATH. "
            "Install the Planetary GRASS addons (make install) first.")
    out = "{}_ac_{}".format(tmp_prefix, band_name)
    kw = {
        "input":     band_name,
        "output":    out,
        "model":     atcorr_params["model"],
        "incidence": atcorr_params["incidence"],
        "emission":  atcorr_params["emission"],
        "phase":     atcorr_params["phase"],
        "tau":       str(atcorr_params["tau"]),
        "wha":       str(atcorr_params["wha"]),
    }
    if atcorr_params.get("hnorm"):
        kw["hnorm"] = str(atcorr_params["hnorm"])
    if atcorr_params.get("bha"):
        kw["bha"] = str(atcorr_params["bha"])
    gs.run_command("p.atcorr.hapke", overwrite=True, quiet=True, **kw)
    return out


def _atcorr_group(band_names, wl_dict, tmp_prefix, atcorr_params):
    """Atmospherically correct all bands; return updated band_names and wl_dict."""
    gs.message("Applying Hapke atmospheric correction to {} bands …".format(
        len(band_names)))
    new_names = []
    new_wl_dict = {}
    for bn in band_names:
        corr = _atcorr_band(bn, tmp_prefix, atcorr_params)
        new_names.append(corr)
        new_wl_dict[corr] = wl_dict[bn]
    return new_names, new_wl_dict


# ── Phase 4.1 — NNLS spectral unmixing ───────────────────────────────────────

def _read_all_bands(band_names):
    """Return list of 2-D float64 arrays, one per band."""
    return [_read_band(n) for n in band_names]


def _unmix_nnls(em_matrix, band_stack, min_abund=0.01):
    """Per-pixel non-negative least-squares spectral unmixing.

    em_matrix  : (nEndmembers, nBands) — library endmember spectra
    band_stack : list of nBands 2-D (nRows, nCols) float64 arrays
    min_abund  : pixels with abundance below this threshold → NaN
    Returns    : list of nEndmembers 2-D abundance arrays in [0, 1]
    """
    import numpy as np
    try:
        from scipy.optimize import nnls
    except ImportError:
        gs.fatal(
            "scipy is required for NNLS unmixing (-u). "
            "Install with: pip install scipy")

    nEM, nBands = em_matrix.shape
    nRows, nCols = band_stack[0].shape
    A = em_matrix.T  # (nBands, nEM)
    stack = np.stack(band_stack, axis=0)  # (nBands, nRows, nCols)
    abund = [np.full((nRows, nCols), float("nan")) for _ in range(nEM)]

    for row in range(nRows):
        col_spec = stack[:, row, :]   # (nBands, nCols)
        for col in range(nCols):
            r = col_spec[:, col]
            if np.any(np.isnan(r)):
                continue
            x, _ = nnls(A, r)
            s = x.sum()
            if s > 1e-10:
                x = x / s          # normalize abundances to sum = 1
            for i in range(nEM):
                if x[i] >= min_abund:
                    abund[i][row, col] = float(x[i])
    return abund


# ── Raster I/O ────────────────────────────────────────────────────────────────

def _read_band(band_name):
    """Read a GRASS raster into a float64 numpy array; NULL → NaN."""
    import numpy as np
    reg = gs.region()
    nr, nc = int(reg["rows"]), int(reg["cols"])
    tmp = tempfile.mktemp(suffix=".bin")
    gs.run_command("r.out.bin", input=band_name, output=tmp,
                   bytes=4, flags="f", null=str(_NULL), quiet=True)
    arr = np.fromfile(tmp, dtype=np.float32).reshape(nr, nc).astype(np.float64)
    arr[arr == _NULL] = float("nan")
    os.unlink(tmp)
    return arr


def _write_band(arr, map_name, region):
    """Write float64 numpy array as GRASS DCELL raster (current region)."""
    import numpy as np
    tmp = tempfile.mktemp(suffix=".bin")
    flat = arr.astype("float32")
    flat_null = np.where(np.isnan(flat), _NULL, flat)
    flat_null.tofile(tmp)
    gs.run_command(
        "r.in.bin",
        input=tmp, output=map_name,
        bytes=4, flags="f",
        north=region["n"], south=region["s"],
        east=region["e"],  west=region["w"],
        rows=int(region["rows"]), cols=int(region["cols"]),
        anull=str(_NULL),
        overwrite=True, quiet=True,
    )
    os.unlink(tmp)


def _set_colors(map_name):
    """Apply blue-to-red band-depth colour table."""
    gs.run_command("r.colors", map=map_name, color="ryb", quiet=True)


# ── Spectral computation ──────────────────────────────────────────────────────

def _band_depth(r_left, r_center, r_right, wl_left, wl_center, wl_right):
    """
    Clark & Roush (1984) linear-continuum absorption band depth:
        BD = 1 - R_center / R_continuum
    where R_continuum is linearly interpolated at wl_center between
    (wl_left, R_left) and (wl_right, R_right).
    Returns array in [0, 1] (NaN where invalid).
    """
    import numpy as np
    span = wl_right - wl_left
    if abs(span) < 1.0e-10:
        return np.full_like(r_center, float("nan"))
    t = (wl_center - wl_left) / span
    r_cont = r_left * (1.0 - t) + r_right * t
    with np.errstate(invalid="ignore", divide="ignore"):
        bd = 1.0 - r_center / r_cont
    bd = np.where(r_cont <= 0.0, float("nan"), bd)
    return np.clip(bd, 0.0, 1.0)


def _detect_species(species, wl_dict, min_bd, temperature_K=None):
    """
    Compute weighted multi-feature absorption band depth for a species.

    Primary feature (i=0) weight = 1.0; confirming features = 0.6.
    temperature_K: if given, shift ice band centers before band matching (Phase 4.2).
    Returns (bd_array, note_str) where bd_array is None if no bands covered.
    """
    import numpy as np

    feat_list = species.get("absorption_bands", [])
    if not feat_list:
        return None, "no absorption bands defined"

    bd_sum, w_sum = None, 0.0
    covered, skipped = [], []

    for i, ab in enumerate(feat_list):
        wl_c = _temp_corrected_center(ab, temperature_K)
        wl_l = ab.get("left",  wl_c * 0.950)
        wl_r = ab.get("right", wl_c * 1.050)

        bn_c, actual_c = _find_nearest_band(wl_dict, wl_c)
        bn_l, actual_l = _find_nearest_band(wl_dict, wl_l)
        bn_r, actual_r = _find_nearest_band(wl_dict, wl_r)

        if bn_c is None:
            skipped.append("{:.4f}µm (no band)".format(wl_c))
            continue
        if bn_l is None or bn_r is None:
            skipped.append("{:.4f}µm (no shoulder)".format(wl_c))
            continue
        if bn_l == bn_c or bn_r == bn_c:
            skipped.append("{:.4f}µm (degenerate — left/right == center)".format(wl_c))
            continue

        r_c = _read_band(bn_c)
        r_l = _read_band(bn_l)
        r_r = _read_band(bn_r)

        bd = _band_depth(r_l, r_c, r_r, actual_l, actual_c, actual_r)

        w = 1.0 if i == 0 else 0.6
        if bd_sum is None:
            bd_sum = np.zeros_like(bd)
        bd_sum += bd * w
        w_sum  += w
        covered.append("{:.4f}µm".format(wl_c))

    if bd_sum is None or w_sum == 0.0:
        return None, "skipped: " + "; ".join(skipped), 0, len(feat_list)

    bd_final = bd_sum / w_sum
    bd_final[bd_final < min_bd] = float("nan")

    note = "features: " + ",".join(covered)
    if skipped:
        note += " | gaps: " + ",".join(skipped)
    return bd_final, note, len(covered), len(feat_list)


# ── List mode ────────────────────────────────────────────────────────────────

def _print_species_list(body, sensor_min, sensor_max, in_range, out_range):
    print("\n{} — sensor {:.4f}–{:.4f} µm".format(body.upper(), sensor_min, sensor_max))
    print("\nDetectable ({}):\n".format(len(in_range)))
    for sp in in_range:
        centers = ", ".join(
            "{:.4f} µm".format(ab["center"])
            for ab in sp.get("absorption_bands", [])
        )
        print("  {:40s}  {:10s}  {}".format(
            sp["name"], sp.get("_mtype", "?"), centers))
    if out_range:
        print("\nOut of sensor range ({}):\n".format(len(out_range)))
        for sp in out_range:
            dr = sp.get("detection_range_um", ["?", "?"])
            print("  {:40s}  needs {:.4f}–{:.4f} µm".format(
                sp["name"], float(dr[0]), float(dr[1])))


# ── Cleanup ──────────────────────────────────────────────────────────────────

def _cleanup_ac_maps(map_names):
    """Remove temporary atmospherically-corrected rasters."""
    for m in map_names:
        gs.run_command("g.remove", flags="f", type="raster",
                       name=m, quiet=True)


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    import numpy as np

    options, flags = gs.parser()

    group        = options["group"]
    body         = options["body"]
    out_prefix   = options["output_prefix"]
    wcsv         = options["wavelengths"] or None
    matter_types = [m.strip() for m in options["matter"].split(",")]
    db_path      = options["db"] or None
    min_bd       = float(options["min_bd"])
    mode         = options["mode"]  # "reflectance" or "emissivity"
    flag_list    = flags["l"]
    flag_unmix   = flags["u"]       # Phase 4.1 NNLS
    flag_atcorr  = flags["a"]       # Phase 4.4 atmospheric correction
    flag_comp    = flags["c"]
    flag_verbose = flags["v"]
    flag_quality = flags["q"]       # Phase 5.1 confidence rasters

    # Phase 5 options
    min_conf  = float(options["min_conf"])
    report_path = options["report"] or None

    # Phase 4.1 — NNLS
    em_group   = options["endmembers"] or None
    min_abund  = float(options["min_abund"])
    if flag_unmix and not em_group:
        gs.fatal("-u (NNLS unmixing) requires endmembers= group.")

    # Phase 4.2 — temperature correction
    temperature_K = float(options["temperature"]) if options["temperature"] else None

    # Phase 4.3 — space weathering
    sw_factor = float(options["space_weathering"])
    body_meta = db_path and {}  # loaded after DB; placeholder
    sw_alpha  = 0.0             # resolved after DB load below

    # Phase 4.4 — atmospheric correction
    atcorr_params = None
    if flag_atcorr:
        for req in ["atcorr_incidence", "atcorr_emission", "atcorr_phase",
                    "atcorr_tau", "atcorr_wha"]:
            if not options[req]:
                gs.fatal("--atcorr requires {}=".format(req))
        atcorr_params = {
            "incidence": options["atcorr_incidence"],
            "emission":  options["atcorr_emission"],
            "phase":     options["atcorr_phase"],
            "model":     options["atcorr_model"],
            "tau":       options["atcorr_tau"],
            "wha":       options["atcorr_wha"],
        }

    if "all" in matter_types:
        matter_types = ["minerals", "ices", "gases", "organics", "liquids"]

    # ── Load database ─────────────────────────────────────────────────────────
    db = _load_database(db_path)
    body_data = db.get("bodies", {}).get(body)
    if body_data is None and body != "generic":
        gs.fatal(
            "Body '{}' not in database. Available: {}".format(
                body, ", ".join(db.get("bodies", {}).keys()))
        )

    all_species = []
    if body == "generic":
        for bdata in db.get("bodies", {}).values():
            for mtype in matter_types:
                for sp in bdata.get(mtype, []):
                    sp = dict(sp); sp["_mtype"] = mtype
                    all_species.append(sp)
    else:
        for mtype in matter_types:
            for sp in body_data.get(mtype, []):
                sp = dict(sp); sp["_mtype"] = mtype
                all_species.append(sp)

    if not all_species:
        gs.warning("No species found for body='{}', matter={}.".format(
            body, matter_types))
        return

    # Phase 4.3 — resolve space-weathering alpha from body_meta
    sw_alpha = db.get("body_meta", {}).get(body, {}).get("sw_alpha", 0.0)
    if sw_factor > 0.0 and sw_alpha == 0.0:
        gs.warning(
            "space_weathering={} given but body '{}' has no sw_alpha in body_meta "
            "(correction not applicable). Ignored.".format(sw_factor, body))
        sw_factor = 0.0

    # ── Resolve wavelengths ───────────────────────────────────────────────────
    band_names = _get_group_bands(group)
    if not band_names:
        gs.fatal("No bands found in group '{}'.".format(group))

    if wcsv:
        wls, _ = _read_wavelengths_csv(wcsv)
        if len(wls) != len(band_names):
            gs.fatal(
                "wavelengths= CSV has {} entries but group '{}' has {} bands.".format(
                    len(wls), group, len(band_names))
            )
        wl_dict = _build_wl_dict(band_names, wls)
    else:
        sidecar = _get_sidecar_wavelengths(band_names)
        if sidecar is not None:
            wl_dict = _build_wl_dict(band_names, sidecar)
        else:
            gs.fatal(
                "No wavelength data found. Provide wavelengths= (two-column CSV: "
                "wavelength_um,fwhm_um per band line, one line per band in the group)."
                "\nAlternatively, import with p.in.isis to preserve per-band "
                "wavelength metadata automatically.")

    sensor_min = min(wl_dict.values())
    sensor_max = max(wl_dict.values())

    # Phase 4.4 — atmospheric correction pre-step (replaces band_names/wl_dict)
    ac_tmp_maps = []
    if flag_atcorr:
        tmp_pfx = "{}__ac_tmp".format(out_prefix)
        band_names, wl_dict = _atcorr_group(band_names, wl_dict, tmp_pfx, atcorr_params)
        ac_tmp_maps = list(band_names)  # track for cleanup

    # ── Filter species by sensor coverage and mode ───────────────────────────
    in_range, out_range = [], []
    for sp in all_species:
        dr = sp.get("detection_range_um", [0.0, 1000.0])
        sp_mode = sp.get("mode", "reflectance")  # "reflectance" or "emissivity"
        if sp_mode != mode:
            out_range.append(sp)
            continue
        if float(dr[0]) <= sensor_max and float(dr[1]) >= sensor_min:
            in_range.append(sp)
        else:
            out_range.append(sp)

    gs.message(
        "Body: {} | Bands: {} | Sensor: {:.4f}–{:.4f} µm | "
        "In range: {} | Out of range: {} | Mode: {}".format(
            body, len(band_names), sensor_min, sensor_max,
            len(in_range), len(out_range), mode)
    )

    if flag_list:
        _print_species_list(body, sensor_min, sensor_max, in_range, out_range)
        return

    region = gs.region()
    output_maps = {mt: [] for mt in ["minerals", "ices", "gases", "organics", "liquids"]}

    # Phase 5.2 — detection report accumulator
    report_data = {
        "body": body, "mode": mode,
        "sensor_min_um": sensor_min, "sensor_max_um": sensor_max,
        "n_bands": len(band_names),
        "in_range": len(in_range), "out_of_range": len(out_range),
        "detections": [], "skipped": [],
    }

    # ── Phase 4.1 — NNLS spectral unmixing ───────────────────────────────────
    if flag_unmix:
        em_band_names = _get_group_bands(em_group)
        if len(em_band_names) != len(band_names):
            gs.fatal(
                "endmembers= group has {} bands but input group has {} bands — "
                "must match.".format(len(em_band_names), len(band_names)))
        gs.message("NNLS unmixing: {} endmembers × {} bands".format(
            len(em_band_names), len(band_names)))
        em_spectra_list = _read_all_bands(em_band_names)
        em_matrix = np.stack([s.flatten() for s in em_spectra_list], axis=0)
        band_stack = _read_all_bands(band_names)
        abund_maps = _unmix_nnls(em_matrix, band_stack, min_abund)
        for i, em_name in enumerate(em_band_names):
            out_name = "{}_abund_{}".format(out_prefix, em_name)
            _write_band(abund_maps[i], out_name, region)
            gs.run_command("r.support", map=out_name,
                           title="NNLS abundance: {}".format(em_name),
                           description="Endmember {} | NNLS unmixing | body={}".format(
                               em_name, body),
                           overwrite=True, quiet=True)
            n_valid = int(np.sum(~np.isnan(abund_maps[i])))
            gs.message("  {} → {} valid pixels".format(em_name, n_valid))
            output_maps.setdefault("minerals", []).append((out_name, abund_maps[i], 1.0))
        _cleanup_ac_maps(ac_tmp_maps)
        gs.message("NNLS done. {} abundance maps written.".format(len(em_band_names)))
        return

    # ── Compute band depths ───────────────────────────────────────────────────
    for sp in in_range:
        sp_name  = sp["name"]
        mtype    = sp.get("_mtype", "unknown")
        out_name = "{}_{}".format(out_prefix, sp_name)

        gs.message("  [{:8s}] {} …".format(mtype, sp_name))

        bd_arr, note, n_matched, n_total = _detect_species(
            sp, wl_dict, min_bd, temperature_K)
        confidence = n_matched / n_total if n_total > 0 else 0.0

        if bd_arr is None:
            gs.message("    Skipped: {}".format(note))
            report_data["skipped"].append(
                {"name": sp_name, "mtype": mtype, "reason": note})
            continue

        # Phase 5 — minimum confidence gate
        if confidence < min_conf:
            gs.message(
                "    Skipped: confidence {}/{} = {:.2f} < min_conf={:.2f}".format(
                    n_matched, n_total, confidence, min_conf))
            report_data["skipped"].append(
                {"name": sp_name, "mtype": mtype,
                 "reason": "confidence {:.2f} < min_conf {:.2f}".format(
                     confidence, min_conf)})
            continue

        # Phase 4.3 — space weathering correction
        if sw_factor > 0.0:
            bd_arr = _apply_space_weathering(bd_arr, sw_factor, sw_alpha)
            note += " | SW-corr α={:.2f} Is/FeO={:.2f}".format(sw_alpha, sw_factor)

        n_valid = int(np.sum(~np.isnan(bd_arr)))
        if n_valid == 0:
            gs.message("    No pixels exceed min_bd={:.3f}; map not written.".format(min_bd))
            report_data["skipped"].append(
                {"name": sp_name, "mtype": mtype,
                 "reason": "no pixels exceed min_bd={:.3f}".format(min_bd)})
            continue

        _write_band(bd_arr, out_name, region)
        _set_colors(out_name)

        # Phase 5.1 — per-species confidence raster
        if flag_quality:
            conf_arr = np.full_like(bd_arr, confidence)
            conf_arr[np.isnan(bd_arr)] = float("nan")
            conf_map = "{}_conf".format(out_name)
            _write_band(conf_arr, conf_map, region)
            gs.run_command("r.colors", map=conf_map, color="grey", quiet=True)
            gs.run_command("r.support", map=conf_map,
                           title="{} band-concordance confidence".format(sp_name),
                           description="n_matched={} / n_total={} = {:.2f}".format(
                               n_matched, n_total, confidence),
                           overwrite=True, quiet=True)

        sw_desc = " | SW-corr α={:.2f}".format(sw_alpha) if sw_factor > 0.0 else ""
        tc_desc = " | T={:.0f}K".format(temperature_K) if temperature_K else ""
        refs = "; ".join(
            r.get("cite", "") for r in sp.get("refs", [])[:2] if r.get("cite"))
        gs.run_command(
            "r.support", map=out_name,
            title="{} band depth [{}]".format(sp.get("display_name", sp_name), mtype),
            description="{} | Clark&Roush1984 BD | conf={:.2f} | mode={}{}{}{}".format(
                sp.get("formula", sp_name), confidence, mode, tc_desc, sw_desc,
                " | " + refs if refs else ""),
            overwrite=True, quiet=True)

        mean_bd = float(np.nanmean(bd_arr))
        max_bd  = float(np.nanmax(bd_arr))
        output_maps[mtype].append((out_name, bd_arr, confidence))

        # Phase 5.2 — accumulate detection record
        report_data["detections"].append({
            "name": sp_name, "mtype": mtype,
            "n_diagnostic_bands": n_total, "n_matched": n_matched,
            "confidence": round(confidence, 4),
            "n_valid_pixels": n_valid,
            "mean_bd": round(mean_bd, 6),
            "max_bd":  round(max_bd,  6),
            "output_map": out_name,
            "note": note,
        })

        if flag_verbose:
            gs.message(
                "    {} valid pixels | conf={}/{} | mean BD={:.4f} max={:.4f} | {}".format(
                    n_valid, n_matched, n_total,
                    mean_bd, max_bd, note))
        else:
            gs.message("    {} valid pixels | conf={}/{} | {}".format(
                n_valid, n_matched, n_total, note))

    # ── Composite false-colour RGB ────────────────────────────────────────────
    if flag_comp:
        channels = {
            "red":   output_maps.get("minerals", []),
            "green": output_maps.get("ices", []) + output_maps.get("gases", []),
            "blue":  output_maps.get("organics", []) + output_maps.get("liquids", []),
        }
        rgb_maps = []
        for color, maps_list in channels.items():
            if not maps_list:
                placeholder = "{}_{}_zero".format(out_prefix, color)
                gs.run_command(
                    "r.mapcalc",
                    expression="{} = 0".format(placeholder),
                    overwrite=True, quiet=True)
                rgb_maps.append(placeholder)
            else:
                # Phase 5 — weight by confidence × mean BD for defensible composite
                best = max(maps_list,
                           key=lambda x: float(np.nanmean(x[1])) * x[2])[0]
                rgb_maps.append(best)

        comp_group = "{}_composite_RGB".format(out_prefix)
        gs.run_command(
            "i.group", group=comp_group,
            input=",".join(rgb_maps),
            overwrite=True, quiet=True)
        gs.message("Composite RGB group '{}': R={}, G={}, B={}".format(
            comp_group, *rgb_maps))

    _cleanup_ac_maps(ac_tmp_maps)

    # Phase 5.2 — write JSON detection report
    if report_path:
        report_data["n_detections"] = len(report_data["detections"])
        report_data["n_skipped"]    = len(report_data["skipped"])
        with open(report_path, "w") as f:
            json.dump(report_data, f, indent=2)
        gs.message("Detection report written to: {}".format(report_path))

    total = sum(len(v) for v in output_maps.values())
    gs.message("\nDone. {} species maps written with prefix '{}'.".format(
        total, out_prefix))


if __name__ == "__main__":
    main()
