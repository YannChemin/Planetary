#!/usr/bin/env python3
"""
Extract key planetary mineral spectra from USGS splib07a and RELAB
into the built-in fallback spectral library (spectra/planetary/).

Run once from the module's own directory:
    python3 make_speclib.py

Requires USGS splib07a at $HOME/DBDATA/usgs_splib07/ (or override with
SPLIB07_DIR env var) and RELAB 2025 at $HOME/DBDATA/RelabDatabase2025Dec31/
(or override with RELAB_DIR env var).

Output: spectra/planetary/<mineral_name>.csv
Format: 2-column CSV (wavelength_um, reflectance), header line:
  # name=<mineral_name>  source=<USGS|RELAB>  instrument=<BECK|ASD>  class=<class>
"""

import os
import sys
import math

SPLIB07 = os.environ.get("SPLIB07_DIR",
    os.path.join(os.path.expanduser("~"), "DBDATA", "usgs_splib07",
                 "ASCIIdata", "ASCIIdata_splib07a"))
RELAB   = os.environ.get("RELAB_DIR",
    os.path.join(os.path.expanduser("~"), "DBDATA", "RelabDatabase2025Dec31"))
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "spectra", "planetary")

SENTINEL = -1.23e34  # USGS no-data flag

# ── USGS splib07a wavelength arrays ──────────────────────────────────────────
def _read_usgs_wavelengths(fname):
    path = os.path.join(SPLIB07, fname)
    wls = []
    with open(path) as f:
        for i, line in enumerate(f):
            if i == 0: continue  # header
            wls.append(float(line.strip()))
    return wls

# ── Read one USGS spectrum (skip header, handle sentinel) ────────────────────
def _read_usgs_spectrum(chapter_subdir, filename):
    path = os.path.join(SPLIB07, chapter_subdir, filename)
    vals = []
    with open(path) as f:
        for i, line in enumerate(f):
            if i == 0: continue  # header
            v = float(line.strip())
            vals.append(None if abs(v - SENTINEL) < 1e30 else v)
    return vals

# ── Read RELAB spectrum (tab-separated: wavelength, reflectance, stddev) ─────
def _read_relab_spectrum(pi_code, set_code, spec_id):
    for ext in (".txt", ".asc"):
        path = os.path.join(RELAB, "data", pi_code, set_code,
                            spec_id.lower() + ext)
        if os.path.exists(path):
            wls, refs = [], []
            with open(path) as f:
                for i, line in enumerate(f):
                    if i < 2: continue  # 2 header lines
                    parts = line.strip().split()
                    if len(parts) < 2: continue
                    try:
                        wls.append(float(parts[0]))
                        refs.append(float(parts[1]))
                    except ValueError:
                        continue
            return wls, refs
    return None, None

# ── Write output CSV ──────────────────────────────────────────────────────────
def _write_csv(name, mineral_class, source, instrument, wavelengths, reflectances):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name + ".csv")
    n_written = 0
    with open(path, "w") as f:
        f.write(f"# name={name}  source={source}  instrument={instrument}"
                f"  class={mineral_class}\n")
        f.write("wavelength_um,reflectance\n")
        for wl, ref in zip(wavelengths, reflectances):
            if ref is None or math.isnan(ref):
                continue
            f.write(f"{wl:.6f},{ref:.8f}\n")
            n_written += 1
    print(f"  {name:<45} {n_written:4d} pts  → {path}")
    return n_written

# ── USGS BECK helper (480-band Beckman spectra) ───────────────────────────────
def _usgs_beck(name, mineral_class, chapter, filename):
    if not hasattr(_usgs_beck, "_wls"):
        _usgs_beck._wls = _read_usgs_wavelengths(
            "splib07a_Wavelengths_BECK_Beckman_0.2-3.0_microns.txt")
    vals = _read_usgs_spectrum(chapter, filename)
    return _write_csv(name, mineral_class, "USGS_splib07a", "BECK_Beckman",
                      _usgs_beck._wls, vals)

# ── USGS ASD helper (2151-band ASD Full Range spectra) ───────────────────────
def _usgs_asd(name, mineral_class, chapter, filename):
    if not hasattr(_usgs_asd, "_wls"):
        _usgs_asd._wls = _read_usgs_wavelengths(
            "splib07a_Wavelengths_ASD_0.35-2.5_microns_2151_ch.txt")
    vals = _read_usgs_spectrum(chapter, filename)
    return _write_csv(name, mineral_class, "USGS_splib07a", "ASD_FieldSpec",
                      _usgs_asd._wls, vals)

# ── RELAB helper ──────────────────────────────────────────────────────────────
def _relab(name, mineral_class, pi_code, set_code, spec_id):
    wls, refs = _read_relab_spectrum(pi_code, set_code, spec_id)
    if wls is None:
        print(f"  SKIP {name} — RELAB file not found")
        return 0
    return _write_csv(name, mineral_class, "RELAB_2025", "various",
                      wls, refs)

MINERALS = "ChapterM_Minerals"

def main():
    print(f"Output: {OUT_DIR}")
    print(f"USGS splib07a: {SPLIB07}")
    print(f"RELAB: {RELAB}")
    print()

    # ── Olivine (compositional series) ───────────────────────────────────────
    _usgs_beck("olivine_fo89",  "olivine", MINERALS, "splib07a_Olivine_GDS70.b_Fo89_115um_BECKb_AREF.txt")
    _usgs_beck("olivine_fo51",  "olivine", MINERALS, "splib07a_Olivine_KI3188_Fo51_lt60um_BECKb_AREF.txt")
    _usgs_beck("olivine_fo29",  "olivine", MINERALS, "splib07a_Olivine_KI3291_Fo29_lt60um_BECKb_AREF.txt")
    _usgs_beck("olivine_fo11",  "olivine", MINERALS, "splib07a_Olivine_KI3005_Fo11_lt60um_BECKb_AREF.txt")

    # ── Low-Ca pyroxene ───────────────────────────────────────────────────────
    _usgs_beck("lcp_enstatite", "low_ca_pyroxene", MINERALS, "splib07a_Enstatite_NMNH128288_BECKc_AREF.txt")
    _usgs_beck("lcp_bronzite",  "low_ca_pyroxene", MINERALS, "splib07a_Bronzite_HS9.3B_Pyroxene_BECKc_AREF.txt")

    # ── High-Ca pyroxene ──────────────────────────────────────────────────────
    _usgs_beck("hcp_augite",    "high_ca_pyroxene", MINERALS, "splib07a_Augite_NMNH120049_BECKb_AREF.txt")
    _usgs_beck("hcp_diopside",  "high_ca_pyroxene", MINERALS, "splib07a_Diopside_NMNHR18685_~160_Pyx_BECKb_AREF.txt")

    # ── Plagioclase ───────────────────────────────────────────────────────────
    _usgs_beck("plagioclase_anorthite", "plagioclase", MINERALS, "splib07a_Anorthite_GDS28_Syn_lt74um_BECKa_AREF.txt")
    _usgs_beck("plagioclase_albite",    "plagioclase", MINERALS, "splib07a_Albite_HS143.3B_Plagioclase_BECKc_AREF.txt")

    # ── Phyllosilicates ───────────────────────────────────────────────────────
    _usgs_beck("smectite_nontronite",      "phyllosilicate", MINERALS, "splib07a_Nontronite_NG-1.a_BECKb_AREF.txt")
    _usgs_beck("smectite_montmorillonite", "phyllosilicate", MINERALS, "splib07a_Montmorillonite_SWy-1_BECKb_AREF.txt")
    _usgs_beck("smectite_saponite",        "phyllosilicate", MINERALS, "splib07a_Saponite_SapCa-1_BECKb_AREF.txt")
    _usgs_beck("kaolinite",                "phyllosilicate", MINERALS, "splib07a_Kaolinite_CM9_BECKb_AREF.txt")
    _usgs_asd( "serpentine",               "phyllosilicate", MINERALS, "splib07a_Serpentine_HS318.1B_ASDFRc_AREF.txt")
    _usgs_asd( "chlorite",                 "phyllosilicate", MINERALS, "splib07a_Chlorite_HS179.1B_ASDFRb_AREF.txt")
    _usgs_beck("illite",                   "phyllosilicate", MINERALS, "splib07a_Illite_GDS4_Marblehead_BECKb_AREF.txt")
    _usgs_beck("muscovite",                "phyllosilicate", MINERALS, "splib07a_Muscovite_GDS108_BECKb_AREF.txt")
    _usgs_beck("talc",                     "phyllosilicate", MINERALS, "splib07a_Talc_HS21.3B_BECKb_AREF.txt")
    _usgs_beck("prehnite",                 "secondary_silicate", MINERALS, "splib07a_Prehnite_GDS613.a_lt60um_ASDFRa_AREF.txt")
    _usgs_beck("epidote",                  "secondary_silicate", MINERALS, "splib07a_Epidote_GDS26.a_75-200um_BECKb_AREF.txt")

    # ── Carbonates ────────────────────────────────────────────────────────────
    _usgs_beck("carbonate_calcite",   "carbonate", MINERALS, "splib07a_Calcite_CO2004_BECKb_AREF.txt")

    # ── Sulfates ──────────────────────────────────────────────────────────────
    _usgs_beck("sulfate_gypsum",      "sulfate", MINERALS, "splib07a_Gypsum_HS333.3B_(Selenite)_BECKa_AREF.txt")
    _usgs_beck("sulfate_jarosite_na", "sulfate", MINERALS, "splib07a_Jarosite_GDS24_Na_BECKb_AREF.txt")
    _usgs_beck("sulfate_jarosite_k",  "sulfate", MINERALS, "splib07a_Jarosite_JR2501_(K)_BECKb_AREF.txt")

    # ── Iron oxides ───────────────────────────────────────────────────────────
    _usgs_beck("iron_hematite",       "iron_oxide", MINERALS, "splib07a_Hematite_GDS69.a_150-250u_BECKb_AREF.txt")
    _usgs_asd( "iron_goethite",       "iron_oxide", MINERALS, "splib07a_Goethite_GDS134_ASDFRb_AREF.txt")

    # ── Silica / opal ─────────────────────────────────────────────────────────
    _usgs_beck("silica_opal",         "opal", MINERALS, "splib07a_Opal_WS732_BECKa_AREF.txt")

    print(f"\nDone. {len(os.listdir(OUT_DIR))} spectra in {OUT_DIR}")

if __name__ == "__main__":
    main()
