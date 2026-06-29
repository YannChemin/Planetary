#!/usr/bin/env python3
# % module
# % label: Planetary spectral library search — rank mineral matches by SAM angle.
# % description: Compares a query spectrum (from a CSV file or extracted as the mean of a GRASS raster group over the current region) against a spectral library directory. Each library entry is a 2-column CSV (wavelength_um, reflectance). Reports top-N matches ranked by Spectral Angle Mapper (SAM) distance. The built-in fallback library contains 28 key planetary minerals extracted from USGS splib07a (olivine, pyroxene, plagioclase, phyllosilicates, carbonates, sulfates, iron oxides, opal).
# % keyword: Planetary
# % keyword: Spectral & Mineral Mapping
# % keyword: spectral library
# % keyword: SAM
# % keyword: mineral identification
# % end

# %option
# % key: spectrum
# % type: string
# % required: no
# % label: Input spectrum CSV file (two columns: wavelength_um, reflectance)
# % description: Mutually exclusive with group=. One row per band. Lines starting with # are ignored.
# %end

# %option G_OPT_I_GROUP
# % key: group
# % required: no
# % label: GRASS imagery group to extract mean spectrum from
# % description: The mean value per band over the current region is used. Requires wavelengths= to map bands to wavelengths.
# %end

# %option
# % key: wavelengths
# % type: string
# % required: no
# % label: CSV file with per-band wavelengths in µm (one value per line, for group= mode)
# %end

# %option
# % key: library
# % type: string
# % required: no
# % label: Path to library directory of 2-column CSVs (wavelength_um, reflectance)
# % description: If not given, uses the built-in planetary library (28 mineral spectra from USGS splib07a). A RELAB or USGS splib07a directory can also be given directly; see notes.
# %end

# %option
# % key: top
# % type: integer
# % required: no
# % answer: 10
# % label: Number of top matches to report (ranked by SAM angle, ascending)
# %end

# %option
# % key: output
# % type: string
# % required: no
# % label: Output CSV file for the ranked match table (if not given, prints to stdout)
# %end

# %option
# % key: max_angle
# % type: double
# % required: no
# % answer: 1.5708
# % label: Maximum SAM angle in radians to include in results (default π/2 = no filter)
# %end

# %flag
# % key: v
# % label: Verbose: print each library file considered (including skipped)
# %end

import os
import sys
import math
import glob

def _find_builtin_library():
    """Return path to the built-in planetary spectral library."""
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(os.environ.get("GISBASE", ""), "etc", "planetary",
                     "spectra", "planetary"),
        os.path.join(here, "spectra", "planetary"),
        os.path.join(here, "..", "..", "data", "spectra", "planetary"),
    ]
    for c in candidates:
        if os.path.isdir(c):
            return c
    return None

def _read_spectrum_csv(path):
    """Read a 2-column (wavelength_um, reflectance) CSV; returns (wls, refs)."""
    wls, refs = [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if len(parts) < 2:
                continue
            try:
                w = float(parts[0])
                r = float(parts[1])
            except ValueError:
                continue  # skip header row with non-numeric values
            if math.isnan(r) or math.isnan(w):
                continue
            wls.append(w)
            refs.append(r)
    return wls, refs

def _interp(wl_query, wls_lib, refs_lib):
    """
    Resample refs_lib at wl_query wavelengths via linear interpolation.
    Returns list of resampled reflectances (NaN where out of range).
    """
    result = []
    n = len(wls_lib)
    for wq in wl_query:
        if wq < wls_lib[0] or wq > wls_lib[-1]:
            result.append(float("nan"))
            continue
        # Binary search for bracketing interval
        lo, hi = 0, n - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if wls_lib[mid] <= wq:
                lo = mid
            else:
                hi = mid
        if wls_lib[hi] == wls_lib[lo]:
            result.append(refs_lib[lo])
        else:
            t = (wq - wls_lib[lo]) / (wls_lib[hi] - wls_lib[lo])
            result.append(refs_lib[lo] + t * (refs_lib[hi] - refs_lib[lo]))
    return result

def _sam_angle(a, b):
    """SAM angle in radians between two equal-length vectors (skip NaN pairs)."""
    dot = 0.0; na2 = 0.0; nb2 = 0.0
    for ai, bi in zip(a, b):
        if math.isnan(ai) or math.isnan(bi):
            continue
        dot += ai * bi
        na2 += ai * ai
        nb2 += bi * bi
    if na2 <= 0.0 or nb2 <= 0.0:
        return math.pi / 2.0
    cosv = dot / (math.sqrt(na2) * math.sqrt(nb2))
    cosv = max(-1.0, min(1.0, cosv))
    return math.acos(cosv)

def _extract_group_mean(group_name, wavelength_csv):
    """Extract mean spectrum from a GRASS imagery group."""
    try:
        import grass.script as gs
    except ImportError:
        gs.fatal("GRASS Python API not available — run inside GRASS GIS.")

    maps = gs.read_command("i.group", group=group_name,
                           flags="g", quiet=True).strip().splitlines()
    if not maps:
        gs.fatal(f"Group '{group_name}' is empty or does not exist.")

    wls = []
    if wavelength_csv:
        with open(wavelength_csv) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                try:
                    wls.append(float(line.split(",")[0]))
                except ValueError:
                    pass
        if len(wls) != len(maps):
            gs.warning(f"Wavelength file has {len(wls)} entries but group has "
                       f"{len(maps)} maps; using band indices instead.")
            wls = list(range(1, len(maps) + 1))
    else:
        wls = list(range(1, len(maps) + 1))

    refs = []
    for m in maps:
        info = gs.parse_command("r.univar", map=m, flags="g", quiet=True)
        mean = float(info.get("mean", "nan"))
        refs.append(mean)

    return wls, refs

def main():
    try:
        import grass.script as gs
    except ImportError:
        gs = None

    def fatal(msg):
        if gs:
            gs.fatal(msg)
        else:
            print(f"ERROR: {msg}", file=sys.stderr)
            sys.exit(1)

    def message(msg):
        if gs:
            gs.message(msg)
        else:
            print(msg)

    # Parse options (works both inside GRASS and standalone)
    import atexit
    if gs:
        options, flags = gs.parser()
        spectrum_file  = options.get("spectrum", "")
        group_name     = options.get("group", "")
        wavelength_csv = options.get("wavelengths", "")
        library_dir    = options.get("library", "")
        top_n          = int(options.get("top", "10"))
        output_csv     = options.get("output", "")
        max_angle      = float(options.get("max_angle", str(math.pi / 2.0)))
        verbose        = flags.get("v", False)
    else:
        # Standalone mode: simple positional args for testing
        import argparse
        p = argparse.ArgumentParser()
        p.add_argument("--spectrum");  p.add_argument("--group")
        p.add_argument("--wavelengths"); p.add_argument("--library")
        p.add_argument("--top", type=int, default=10)
        p.add_argument("--output"); p.add_argument("--max_angle", type=float, default=math.pi/2)
        p.add_argument("-v", action="store_true")
        a = p.parse_args()
        spectrum_file = a.spectrum or ""; group_name = a.group or ""
        wavelength_csv = a.wavelengths or ""; library_dir = a.library or ""
        top_n = a.top; output_csv = a.output or ""
        max_angle = a.max_angle; verbose = a.v

    # ── Get query spectrum ─────────────────────────────────────────────────
    if spectrum_file and group_name:
        fatal("Specify either spectrum= or group=, not both.")
    if not spectrum_file and not group_name:
        fatal("Specify either spectrum= (CSV) or group= (GRASS imagery group).")

    if spectrum_file:
        if not os.path.exists(spectrum_file):
            fatal(f"Spectrum file '{spectrum_file}' not found.")
        q_wls, q_refs = _read_spectrum_csv(spectrum_file)
        if not q_wls:
            fatal(f"No valid data read from '{spectrum_file}'.")
        message(f"Query spectrum: {len(q_wls)} bands from '{spectrum_file}'")
    else:
        if not gs:
            fatal("group= mode requires GRASS GIS environment.")
        q_wls, q_refs = _extract_group_mean(group_name, wavelength_csv)
        message(f"Query spectrum: {len(q_wls)} bands from group '{group_name}'")

    # ── Locate library ─────────────────────────────────────────────────────
    if not library_dir:
        library_dir = _find_builtin_library()
        if not library_dir:
            fatal("Built-in planetary library not found. "
                  "Specify library= explicitly or reinstall the package.")
        message(f"Using built-in library: {library_dir}")
    elif not os.path.isdir(library_dir):
        fatal(f"Library directory '{library_dir}' does not exist.")
    else:
        message(f"Using library: {library_dir}")

    # ── Load library entries ────────────────────────────────────────────────
    lib_files = sorted(glob.glob(os.path.join(library_dir, "*.csv")))
    if not lib_files:
        fatal(f"No .csv files found in '{library_dir}'.")
    message(f"Library: {len(lib_files)} entries")

    # ── Compute SAM angle for each entry ───────────────────────────────────
    results = []
    for lf in lib_files:
        name = os.path.splitext(os.path.basename(lf))[0]
        lib_class = ""
        try:
            l_wls, l_refs = _read_spectrum_csv(lf)
            # Read class from header
            with open(lf) as f:
                first_line = f.readline()
            if "class=" in first_line:
                lib_class = first_line.split("class=")[-1].strip()
        except Exception as e:
            if verbose:
                message(f"  SKIP {name}: {e}")
            continue
        if not l_wls:
            if verbose:
                message(f"  SKIP {name}: empty")
            continue

        # Resample library spectrum to query wavelengths
        l_at_q = _interp(q_wls, l_wls, l_refs)
        angle = _sam_angle(q_refs, l_at_q)

        if verbose:
            message(f"  {name:<45} angle={angle:.4f} rad ({math.degrees(angle):.2f}°)")

        if angle <= max_angle:
            results.append((angle, name, lib_class, lf))

    if not results:
        message("No library matches within max_angle threshold.")
        return

    results.sort()
    results = results[:top_n]

    # ── Format output ──────────────────────────────────────────────────────
    header = "rank,name,class,sam_angle_rad,sam_angle_deg,library_path"
    rows = []
    for rank, (angle, name, lib_class, lf) in enumerate(results, 1):
        rows.append(f"{rank},{name},{lib_class},{angle:.6f},"
                    f"{math.degrees(angle):.4f},{lf}")

    if output_csv:
        with open(output_csv, "w") as f:
            f.write("# p.spec.library results\n")
            f.write(f"# query={spectrum_file or group_name}  "
                    f"library={library_dir}  top={top_n}\n")
            f.write(header + "\n")
            for r in rows: f.write(r + "\n")
        message(f"Results written to '{output_csv}'")
    else:
        print(header)
        for r in rows:
            print(r)

    message("Best match: " + results[0][1] +
            f"  (SAM={results[0][0]:.4f} rad = {math.degrees(results[0][0]):.2f}°)")

if __name__ == "__main__":
    main()
