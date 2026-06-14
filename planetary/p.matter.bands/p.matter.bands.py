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
# % options: mars,moon,mercury,venus,titan,europa,io,enceladus,ceres,asteroid_c_type,asteroid_s_type,comet,pluto,generic
# % descriptions: mars;Mars surface and atmosphere;moon;Earth's Moon;mercury;Mercury (MESSENGER/BepiColombo);venus;Venus (surface NIR windows + atmosphere);titan;Saturn's Titan (VIMS atmospheric windows);europa;Jupiter's Europa (irradiated icy surface);io;Jupiter's Io (SO2 frost and sulfur);enceladus;Saturn's Enceladus (clean H2O ice + plume);ceres;Dwarf planet Ceres (ammoniated clays);asteroid_c_type;Carbonaceous (C-type) asteroid;asteroid_s_type;Stony (S-type) asteroid;comet;Cometary nucleus and coma;pluto;Pluto / KBO (volatile ices);generic;Generic body — load all database entries
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
# % key: min_bd
# % type: double
# % required: no
# % answer: 0.01
# % label: Minimum band depth threshold [0.0–1.0]
# % description: Output pixels with BD below this value are set to NULL (no detection).
# %end

# %flag
# % key: l
# % description: List detectable species for this body/sensor combination (no raster output)
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
    """
    best_name, best_wl, best_d = None, None, float("inf")
    for name, wl in wl_dict.items():
        if wl is None:
            continue
        d = abs(wl - target_um)
        if d < best_d:
            best_d, best_name, best_wl = d, name, wl
    return (best_name, best_wl) if best_d <= tolerance_um else (None, None)


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


def _detect_species(species, wl_dict, min_bd):
    """
    Compute weighted multi-feature absorption band depth for a species.

    Primary feature (i=0) weight = 1.0; confirming features = 0.6.
    Returns (bd_array, note_str) where bd_array is None if no bands covered.
    """
    import numpy as np

    feat_list = species.get("absorption_bands", [])
    if not feat_list:
        return None, "no absorption bands defined"

    bd_sum, w_sum = None, 0.0
    covered, skipped = [], []

    for i, ab in enumerate(feat_list):
        wl_c = ab["center"]
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
        return None, "skipped: " + "; ".join(skipped)

    bd_final = bd_sum / w_sum
    bd_final[bd_final < min_bd] = float("nan")

    note = "features: " + ",".join(covered)
    if skipped:
        note += " | gaps: " + ",".join(skipped)
    return bd_final, note


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
    flag_list    = flags["l"]
    flag_comp    = flags["c"]
    flag_verbose = flags["v"]

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

    # ── Filter species by sensor coverage ────────────────────────────────────
    in_range, out_range = [], []
    for sp in all_species:
        dr = sp.get("detection_range_um", [0.0, 1000.0])
        if float(dr[0]) <= sensor_max and float(dr[1]) >= sensor_min:
            in_range.append(sp)
        else:
            out_range.append(sp)

    gs.message(
        "Body: {} | Bands: {} | Sensor: {:.4f}–{:.4f} µm | "
        "In range: {} | Out of range: {}".format(
            body, len(band_names), sensor_min, sensor_max,
            len(in_range), len(out_range))
    )

    if flag_list:
        _print_species_list(body, sensor_min, sensor_max, in_range, out_range)
        return

    # ── Compute band depths ───────────────────────────────────────────────────
    region = gs.region()
    output_maps = {mt: [] for mt in ["minerals", "ices", "gases", "organics", "liquids"]}

    for sp in in_range:
        sp_name  = sp["name"]
        mtype    = sp.get("_mtype", "unknown")
        out_name = "{}_{}".format(out_prefix, sp_name)

        gs.message("  [{:8s}] {} …".format(mtype, sp_name))

        bd_arr, note = _detect_species(sp, wl_dict, min_bd)
        if bd_arr is None:
            gs.message("    Skipped: {}".format(note))
            continue

        n_valid = int(np.sum(~np.isnan(bd_arr)))
        if n_valid == 0:
            gs.message("    No pixels exceed min_bd={:.3f}; map not written.".format(min_bd))
            continue

        _write_band(bd_arr, out_name, region)
        _set_colors(out_name)

        refs = "; ".join(
            r.get("cite", "") for r in sp.get("refs", [])[:2] if r.get("cite"))
        gs.run_command(
            "r.support", map=out_name,
            title="{} band depth [{}]".format(sp.get("display_name", sp_name), mtype),
            description="{} | Clark&Roush1984 BD{}".format(
                sp.get("formula", sp_name), " | " + refs if refs else ""),
            overwrite=True, quiet=True)

        output_maps[mtype].append((out_name, bd_arr))

        if flag_verbose:
            gs.message(
                "    {} valid pixels | mean BD={:.4f} max={:.4f} | {}".format(
                    n_valid, float(np.nanmean(bd_arr)),
                    float(np.nanmax(bd_arr)), note))
        else:
            gs.message("    {} valid pixels | {}".format(n_valid, note))

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
                best = max(maps_list, key=lambda x: float(np.nanmean(x[1])))[0]
                rgb_maps.append(best)

        comp_group = "{}_composite_RGB".format(out_prefix)
        gs.run_command(
            "i.group", group=comp_group,
            input=",".join(rgb_maps),
            overwrite=True, quiet=True)
        gs.message("Composite RGB group '{}': R={}, G={}, B={}".format(
            comp_group, *rgb_maps))

    total = sum(len(v) for v in output_maps.values())
    gs.message("\nDone. {} species maps written with prefix '{}'.".format(
        total, out_prefix))


if __name__ == "__main__":
    main()
