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
# % multiple: yes
# % label: Comma-separated list of endmember spectral library groups for NNLS unmixing (-u)
# % description: One image group per endmember. Each group must have the same band count as the input group, in the same wavelength order, with each band a constant raster equal to that endmember's reflectance at the corresponding wavelength. Output: one abundance map [0,1] per endmember group, named <output_prefix>_abund_<group_name>.
# %end

# %option G_OPT_I_GROUP
# % key: sam_library
# % required: no
# % multiple: yes
# % label: Comma-separated list of full-spectrum library groups for Spectral Angle Mapper (-m)
# % description: One image group per reference spectrum, same convention as endmembers=: each group must have the same band count as the input group, in the same wavelength order, with each band a constant raster equal to that reference's value at the corresponding wavelength. Output: one angle map [0,90] degrees per library group, named <output_prefix>_sam_<group_name> (0 = perfect match). If more than one library group is given, also writes <output_prefix>_sam_classification (smallest-angle match per pixel).
# %end

# %option
# % key: sam_library_prefix
# % type: string
# % required: no
# % label: Prefix for per-species full-spectrum reference groups, for spectral cross-check (-s)
# % description: For each detected species, looks for an image group named <sam_library_prefix>_<species_name> (same band count and wavelength order as the input group; one constant-value band per wavelength). If found, the per-pixel Spectral Angle Mapper distance between the full observed spectrum and this reference is computed; pixels above sam_max_angle= are suppressed from the band-depth map. Species without a matching reference group are processed normally (no cross-check applied). Requires -s.
# %end

# %option
# % key: sam_max_angle
# % type: double
# % required: no
# % answer: 30.0
# % label: Maximum SAM angle in degrees for spectral cross-check confirmation (-s)
# % description: Pixels whose full-spectrum SAM angle to the species' reference (sam_library_prefix=) exceeds this threshold are treated as spectrally inconsistent and suppressed from the band-depth output, even if the absorption-band depth alone exceeded min_bd.
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

# %option
# % key: radiometric_noise
# % type: double
# % required: no
# % answer: 0.0
# % label: 1-sigma relative radiometric noise [0.0 = disabled]
# % description: Fractional 1-sigma uncertainty on each input reflectance/emissivity sample (e.g. 0.02 = two percent). Propagated analytically through the band-depth formula to produce per-species uncertainty. Use with -e to write uncertainty rasters.
# %end

# %option
# % key: reference_prefix
# % type: string
# % required: no
# % label: Output prefix from a previous run, for multi-temporal change detection (-d)
# % description: For each detected species, looks for <reference_prefix>_<species_name> (and, if present, <reference_prefix>_<species_name>_unc) and computes <output_prefix>_<species_name>_diff = BD_now - BD_reference. Requires -d.
# %end

# %option
# % key: change_sigma
# % type: double
# % required: no
# % answer: 2.0
# % label: Significance threshold (combined-sigma units) for change detection (-d)
# % description: When uncertainty rasters are available for both epochs (radiometric_noise= used in both runs), pixels with |diff| / sqrt(sigma_now^2 + sigma_ref^2) >= change_sigma are written to <prefix>_<species>_diff_sig.
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
# % key: m
# % description: Spectral Angle Mapper (SAM) cross-validation mode — output per-pixel spectral angle maps against sam_library= reference spectra instead of band-depth maps (requires sam_library=)
# %end

# %flag
# % key: s
# % description: Spectral cross-check — suppress band-depth pixels whose full spectrum disagrees with the species' reference spectrum (requires sam_library_prefix=)
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

# %flag
# % key: d
# % description: Multi-temporal change detection — compute <prefix>_<species>_diff against reference_prefix= maps from a previous run
# %end

# %flag
# % key: e
# % description: Output per-species uncertainty raster (<prefix>_<species>_unc), propagated analytically from radiometric_noise= through the band-depth formula
# %end

# %flag
# % key: k
# % description: Output a dominant-species classification raster (<prefix>_classification) — category code of the species with highest confidence-weighted band depth at each pixel
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


# ── Phase 8 — Spectral Angle Mapper (SAM) cross-validation ──────────────────

def _sam_angle_deg(band_stack, reference):
    """
    Per-pixel Spectral Angle Mapper: angle (degrees) between each pixel's
    observed spectrum (across all input bands) and a single reference
    spectrum (Kruse et al. 1993; mirrors p_spectra_sam()):

        SAM = arccos( dot(s, r) / (|s| x |r|) )

    band_stack : list of nBands 2-D (nRows, nCols) float64 arrays
    reference  : 1-D array of length nBands (library spectrum)
    Returns    : 2-D array of angles in degrees [0, 90], NaN where invalid.
    """
    import numpy as np
    stack = np.stack(band_stack, axis=0)  # (nBands, nRows, nCols)
    ref = reference.reshape(-1, 1, 1)
    dot = np.nansum(stack * ref, axis=0)
    norm_s = np.sqrt(np.nansum(stack ** 2, axis=0))
    norm_r = np.sqrt(np.sum(reference ** 2))
    with np.errstate(invalid="ignore", divide="ignore"):
        cos_theta = dot / (norm_s * norm_r)
    cos_theta = np.clip(cos_theta, -1.0, 1.0)
    angle_deg = np.degrees(np.arccos(cos_theta))
    invalid = (norm_s <= 0.0) | np.all(np.isnan(stack), axis=0)
    return np.where(invalid, float("nan"), angle_deg)


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


def _write_classification(cat_arr, map_name, region, species_names):
    """Write an integer category raster (NaN -> NULL) with r.category labels.

    cat_arr values are 1-based species indices matching species_names order.
    """
    import numpy as np
    tmp = tempfile.mktemp(suffix=".bin")
    flat = np.where(np.isnan(cat_arr), -9999, cat_arr).astype("int32")
    flat.tofile(tmp)
    gs.run_command(
        "r.in.bin",
        input=tmp, output=map_name,
        bytes=4,
        north=region["n"], south=region["s"],
        east=region["e"],  west=region["w"],
        rows=int(region["rows"]), cols=int(region["cols"]),
        anull="-9999",
        overwrite=True, quiet=True,
    )
    os.unlink(tmp)
    rules = "\n".join(
        "{}:{}".format(i + 1, name) for i, name in enumerate(species_names))
    gs.write_command("r.category", map=map_name, rules="-",
                     separator=":", stdin=rules, quiet=True)
    gs.run_command("r.colors", map=map_name, color="random", quiet=True)


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


def _band_depth_uncertainty(r_left, r_center, r_right, wl_left, wl_center, wl_right,
                            rel_noise):
    """
    Propagate 1-sigma relative radiometric noise through the linear-continuum
    band-depth formula (analytic first-order error propagation):

        BD = 1 - R_c / R_cont,   R_cont = R_l*(1-t) + R_r*t

        dBD/dR_c = -1 / R_cont
        dBD/dR_l = R_c*(1-t) / R_cont^2
        dBD/dR_r = R_c*t     / R_cont^2

        sigma_x  = rel_noise * R_x      (independent per-band noise)
        sigma_BD = sqrt( (dBD/dR_c * sigma_c)^2
                        + (dBD/dR_l * sigma_l)^2
                        + (dBD/dR_r * sigma_r)^2 )

    Returns an array of 1-sigma BD uncertainties (NaN where invalid).
    """
    import numpy as np
    span = wl_right - wl_left
    if abs(span) < 1.0e-10 or rel_noise <= 0.0:
        return np.full_like(r_center, float("nan"))
    t = (wl_center - wl_left) / span
    r_cont = r_left * (1.0 - t) + r_right * t
    with np.errstate(invalid="ignore", divide="ignore"):
        d_c = -1.0 / r_cont
        d_l = r_center * (1.0 - t) / (r_cont ** 2)
        d_r = r_center * t / (r_cont ** 2)
        sigma_c = rel_noise * np.abs(r_center)
        sigma_l = rel_noise * np.abs(r_left)
        sigma_r = rel_noise * np.abs(r_right)
        unc = np.sqrt((d_c * sigma_c) ** 2 + (d_l * sigma_l) ** 2 + (d_r * sigma_r) ** 2)
    unc = np.where(r_cont <= 0.0, float("nan"), unc)
    return unc


def _detect_species(species, wl_dict, min_bd, temperature_K=None, radiometric_noise=0.0):
    """
    Compute weighted multi-feature absorption band depth for a species.

    Primary feature (i=0) weight = 1.0; confirming features = 0.6.
    temperature_K: if given, shift ice band centers before band matching (Phase 4.2).
    radiometric_noise: if > 0, propagate 1-sigma relative noise through the BD
        formula and return a combined uncertainty array (Phase 6.2).
    Returns (bd_array, note_str, n_matched, n_total, unc_array) where bd_array
    and unc_array are None if no bands covered or noise propagation disabled.
    """
    import numpy as np

    feat_list = species.get("absorption_bands", [])
    if not feat_list:
        return None, "no absorption bands defined", 0, 0, None

    bd_sum, w_sum = None, 0.0
    unc_sum_sq = None
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

        if radiometric_noise > 0.0:
            sigma = _band_depth_uncertainty(
                r_l, r_c, r_r, actual_l, actual_c, actual_r, radiometric_noise)
            if unc_sum_sq is None:
                unc_sum_sq = np.zeros_like(bd)
            unc_sum_sq += (w ** 2) * (sigma ** 2)

    if bd_sum is None or w_sum == 0.0:
        return None, "skipped: " + "; ".join(skipped), 0, len(feat_list), None

    bd_final = bd_sum / w_sum
    bd_final[bd_final < min_bd] = float("nan")

    unc_final = None
    if unc_sum_sq is not None:
        unc_final = np.sqrt(unc_sum_sq) / w_sum
        unc_final[np.isnan(bd_final)] = float("nan")

    note = "features: " + ",".join(covered)
    if skipped:
        note += " | gaps: " + ",".join(skipped)
    return bd_final, note, len(covered), len(feat_list), unc_final


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
    flag_uncert  = flags["e"]       # Phase 6.2 uncertainty rasters
    flag_classify = flags["k"]      # Phase 6.1 classification map
    flag_diff    = flags["d"]       # Phase 7.1 multi-temporal change detection
    flag_sam     = flags["m"]       # Phase 8.1 Spectral Angle Mapper
    flag_speccheck = flags["s"]     # Phase 9.1 per-species spectral cross-check

    # Phase 5 options
    min_conf  = float(options["min_conf"])
    report_path = options["report"] or None

    # Phase 6.2 — radiometric noise propagation
    radiometric_noise = float(options["radiometric_noise"])
    if flag_uncert and radiometric_noise <= 0.0:
        gs.warning("-e given but radiometric_noise=0.0 (disabled) — no uncertainty computed.")

    # Phase 7.1 — multi-temporal change detection
    reference_prefix = options["reference_prefix"] or None
    change_sigma = float(options["change_sigma"])
    if flag_diff and not reference_prefix:
        gs.fatal("-d (change detection) requires reference_prefix=.")

    # Phase 4.1 — NNLS
    em_group   = options["endmembers"] or None
    min_abund  = float(options["min_abund"])
    if flag_unmix and not em_group:
        gs.fatal("-u (NNLS unmixing) requires endmembers= group.")

    # Phase 8.1 — Spectral Angle Mapper
    sam_library = options["sam_library"] or None
    if flag_sam and not sam_library:
        gs.fatal("-m (SAM matching) requires sam_library= group(s).")

    # Phase 9.1 — per-species spectral cross-check
    sam_library_prefix = options["sam_library_prefix"] or None
    sam_max_angle = float(options["sam_max_angle"])
    if flag_speccheck and not sam_library_prefix:
        gs.fatal("-s (spectral cross-check) requires sam_library_prefix=.")

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
        em_group_names = [g.strip() for g in em_group.split(",") if g.strip()]
        em_spectra = []
        for g in em_group_names:
            g_bands = _get_group_bands(g)
            if len(g_bands) != len(band_names):
                gs.fatal(
                    "endmembers group '{}' has {} bands but input group has "
                    "{} bands — must match.".format(
                        g, len(g_bands), len(band_names)))
            g_arrays = _read_all_bands(g_bands)
            em_spectra.append(np.array([float(np.nanmean(a)) for a in g_arrays]))
        em_matrix = np.stack(em_spectra, axis=0)  # (nEM, nBands)
        gs.message("NNLS unmixing: {} endmembers × {} bands".format(
            len(em_group_names), len(band_names)))
        band_stack = _read_all_bands(band_names)
        abund_maps = _unmix_nnls(em_matrix, band_stack, min_abund)
        for i, g in enumerate(em_group_names):
            out_name = "{}_abund_{}".format(out_prefix, g)
            _write_band(abund_maps[i], out_name, region)
            gs.run_command("r.support", map=out_name,
                           title="NNLS abundance: {}".format(g),
                           description="Endmember group {} | NNLS unmixing | body={}".format(
                               g, body),
                           overwrite=True, quiet=True)
            n_valid = int(np.sum(~np.isnan(abund_maps[i])))
            gs.message("  {} → {} valid pixels".format(g, n_valid))
            output_maps.setdefault("minerals", []).append((out_name, abund_maps[i], 1.0))
        _cleanup_ac_maps(ac_tmp_maps)
        gs.message("NNLS done. {} abundance maps written.".format(len(em_group_names)))
        return

    # ── Phase 8.1 — Spectral Angle Mapper cross-validation ───────────────────
    if flag_sam:
        sam_group_names = [g.strip() for g in sam_library.split(",") if g.strip()]
        band_stack = _read_all_bands(band_names)
        angle_maps = []
        for g in sam_group_names:
            g_bands = _get_group_bands(g)
            if len(g_bands) != len(band_names):
                gs.fatal(
                    "sam_library group '{}' has {} bands but input group has "
                    "{} bands — must match.".format(
                        g, len(g_bands), len(band_names)))
            g_arrays = _read_all_bands(g_bands)
            ref_vec = np.array([float(np.nanmean(a)) for a in g_arrays])
            angle_deg = _sam_angle_deg(band_stack, ref_vec)
            out_name = "{}_sam_{}".format(out_prefix, g)
            _write_band(angle_deg, out_name, region)
            gs.run_command("r.colors", map=out_name, color="byr", quiet=True)
            gs.run_command("r.support", map=out_name,
                           title="SAM angle vs {}".format(g),
                           description="Spectral Angle Mapper [degrees], "
                                        "0=perfect match, 90=orthogonal",
                           overwrite=True, quiet=True)
            n_valid = int(np.sum(~np.isnan(angle_deg)))
            mean_angle = float(np.nanmean(angle_deg)) if n_valid > 0 else float("nan")
            gs.message("  SAM {} → {} valid pixels | mean angle={:.2f}°".format(
                g, n_valid, mean_angle))
            angle_maps.append((g, angle_deg))

        if len(angle_maps) > 1:
            stack_angles = np.stack([a for (_, a) in angle_maps], axis=0)
            valid_mask = ~np.all(np.isnan(stack_angles), axis=0)
            safe_stack = np.where(np.isnan(stack_angles), np.inf, stack_angles)
            best_idx = np.argmin(safe_stack, axis=0).astype(float) + 1.0
            best_idx[~valid_mask] = float("nan")
            class_map = "{}_sam_classification".format(out_prefix)
            names = [g for (g, _) in angle_maps]
            _write_classification(best_idx, class_map, region, names)
            gs.message("SAM classification map '{}' written ({} categories).".format(
                class_map, len(names)))

        _cleanup_ac_maps(ac_tmp_maps)
        gs.message("SAM matching done. {} angle maps written.".format(len(angle_maps)))
        return

    # ── Compute band depths ───────────────────────────────────────────────────
    classification_entries = []  # Phase 6.1: (sp_name, bd_arr, confidence)

    # Phase 9.1 — full input spectrum, read once, reused for every species'
    # spectral cross-check (avoids re-reading all bands per species).
    full_spectrum_stack = None
    if flag_speccheck:
        full_spectrum_stack = _read_all_bands(band_names)

    for sp in in_range:
        sp_name  = sp["name"]
        mtype    = sp.get("_mtype", "unknown")
        out_name = "{}_{}".format(out_prefix, sp_name)

        gs.message("  [{:8s}] {} …".format(mtype, sp_name))

        bd_arr, note, n_matched, n_total, unc_arr = _detect_species(
            sp, wl_dict, min_bd, temperature_K, radiometric_noise)
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
            denom = 1.0 - sw_alpha * sw_factor
            bd_arr = _apply_space_weathering(bd_arr, sw_factor, sw_alpha)
            if unc_arr is not None and abs(denom) > 1e-6:
                unc_arr = unc_arr / denom  # linear correction scales sigma identically
            note += " | SW-corr α={:.2f} Is/FeO={:.2f}".format(sw_alpha, sw_factor)

        # Phase 9.1 — per-species spectral cross-check
        sam_angle_deg, sam_confirmed_fraction = None, None
        if flag_speccheck:
            ref_group = "{}_{}".format(sam_library_prefix, sp_name)
            ref_bands = _get_group_bands(ref_group) if gs.find_file(
                ref_group, element="group")["name"] else []
            if not ref_bands:
                gs.message(
                    "    No spectral reference group '{}' — cross-check skipped.".format(
                        ref_group))
            elif len(ref_bands) != len(band_names):
                gs.warning(
                    "sam_library_prefix group '{}' has {} bands but input group "
                    "has {} bands — cross-check skipped for this species.".format(
                        ref_group, len(ref_bands), len(band_names)))
            else:
                ref_arrays = _read_all_bands(ref_bands)
                ref_vec = np.array([float(np.nanmean(a)) for a in ref_arrays])
                angle_arr = _sam_angle_deg(full_spectrum_stack, ref_vec)
                pre_valid = int(np.sum(~np.isnan(bd_arr)))
                bd_arr = np.where(angle_arr > sam_max_angle, float("nan"), bd_arr)
                post_valid = int(np.sum(~np.isnan(bd_arr)))
                sam_angle_deg = float(np.nanmean(angle_arr)) if np.any(
                    ~np.isnan(angle_arr)) else None
                sam_confirmed_fraction = (
                    post_valid / pre_valid if pre_valid > 0 else None)
                note += " | SAM cross-check: mean angle={}° kept {}/{} px".format(
                    "{:.2f}".format(sam_angle_deg) if sam_angle_deg is not None else "n/a",
                    post_valid, pre_valid)

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

        # Phase 6.2 — per-species uncertainty raster
        mean_unc = None
        if unc_arr is not None:
            mean_unc = float(np.nanmean(unc_arr)) if n_valid > 0 else None
            if flag_uncert:
                unc_map = "{}_unc".format(out_name)
                _write_band(unc_arr, unc_map, region)
                gs.run_command("r.colors", map=unc_map, color="grey", quiet=True)
                gs.run_command("r.support", map=unc_map,
                               title="{} band-depth uncertainty (1-sigma)".format(sp_name),
                               description="Propagated from radiometric_noise={:.4f}".format(
                                   radiometric_noise),
                               overwrite=True, quiet=True)

        # Phase 7.1 — multi-temporal change detection
        mean_diff, max_abs_diff, n_sig = None, None, None
        if flag_diff:
            ref_map = "{}_{}".format(reference_prefix, sp_name)
            if not gs.find_file(ref_map, element="cell")["name"]:
                gs.message("    No reference map '{}' — skipping change detection.".format(
                    ref_map))
            else:
                ref_arr = _read_band(ref_map)
                diff_arr = bd_arr - ref_arr
                diff_map = "{}_diff".format(out_name)
                _write_band(diff_arr, diff_map, region)
                gs.run_command("r.colors", map=diff_map, color="differences", quiet=True)
                gs.run_command(
                    "r.support", map=diff_map,
                    title="{} band-depth change vs reference".format(sp_name),
                    description="diff = BD_now - BD_ref({})".format(reference_prefix),
                    overwrite=True, quiet=True)
                if np.any(~np.isnan(diff_arr)):
                    mean_diff = float(np.nanmean(diff_arr))
                    max_abs_diff = float(np.nanmax(np.abs(diff_arr[~np.isnan(diff_arr)])))

                ref_unc_map = "{}_unc".format(ref_map)
                if unc_arr is not None and gs.find_file(ref_unc_map, element="cell")["name"]:
                    ref_unc_arr = _read_band(ref_unc_map)
                    combined_sigma = np.sqrt(unc_arr ** 2 + ref_unc_arr ** 2)
                    with np.errstate(invalid="ignore", divide="ignore"):
                        z = diff_arr / combined_sigma
                    sig_mask = np.abs(z) >= change_sigma
                    n_sig = int(np.sum(sig_mask & ~np.isnan(diff_arr)))
                    diff_sig_arr = np.where(sig_mask, diff_arr, float("nan"))
                    sig_map = "{}_diff_sig".format(out_name)
                    _write_band(diff_sig_arr, sig_map, region)
                    gs.run_command("r.colors", map=sig_map, color="differences", quiet=True)
                    gs.run_command(
                        "r.support", map=sig_map,
                        title="{} statistically significant change (|z|>={:.1f}σ)".format(
                            sp_name, change_sigma),
                        overwrite=True, quiet=True)

                gs.message(
                    "    Change vs reference '{}': mean diff={}{}".format(
                        reference_prefix,
                        "{:.4f}".format(mean_diff) if mean_diff is not None else "n/a",
                        " | {} significant px".format(n_sig) if n_sig is not None else ""))

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
        classification_entries.append((sp_name, bd_arr, confidence))

        # Phase 5.2 — accumulate detection record
        report_data["detections"].append({
            "name": sp_name, "mtype": mtype,
            "n_diagnostic_bands": n_total, "n_matched": n_matched,
            "confidence": round(confidence, 4),
            "n_valid_pixels": n_valid,
            "mean_bd": round(mean_bd, 6),
            "max_bd":  round(max_bd,  6),
            "mean_uncertainty": round(mean_unc, 6) if mean_unc is not None else None,
            "mean_diff": round(mean_diff, 6) if mean_diff is not None else None,
            "max_abs_diff": round(max_abs_diff, 6) if max_abs_diff is not None else None,
            "n_significant_change_pixels": n_sig,
            "sam_angle_deg": round(sam_angle_deg, 4) if sam_angle_deg is not None else None,
            "sam_confirmed_fraction": (
                round(sam_confirmed_fraction, 4)
                if sam_confirmed_fraction is not None else None),
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

    # ── Phase 6.1 — dominant-species classification map ──────────────────────
    if flag_classify:
        if classification_entries:
            score_stack = np.stack(
                [bd * conf for (_, bd, conf) in classification_entries], axis=0)
            valid_mask = ~np.all(np.isnan(score_stack), axis=0)
            safe_stack = np.where(np.isnan(score_stack), -np.inf, score_stack)
            cat_idx = np.argmax(safe_stack, axis=0).astype(float) + 1.0
            cat_idx[~valid_mask] = float("nan")
            class_map = "{}_classification".format(out_prefix)
            species_names = [e[0] for e in classification_entries]
            _write_classification(cat_idx, class_map, region, species_names)
            gs.message("Classification map '{}' written ({} categories).".format(
                class_map, len(species_names)))
        else:
            gs.warning(
                "-k given but no species were detected; "
                "classification map not written.")

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
