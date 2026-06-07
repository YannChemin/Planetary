#!/usr/bin/env python3
############################################################################
# MODULE:       p.in.ancillary
# PURPOSE:      Import ancillary planetary data layers (thermal, mineralogy,
#               volatile proxies, crater databases, geologic units).
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Import ancillary planetary data layers for landing-site analysis.
# % keyword: Planetary
# % keyword: Import & Export
# % keyword: import
# % keyword: ancillary
# %end

# %option G_OPT_F_INPUT
# % key: input
# % label: Input file (raster, vector, or crater CSV)
# % required: yes
# %end

# %option G_OPT_R_OUTPUT
# % key: output
# % label: Output GRASS map name (raster or vector)
# % required: yes
# %end

# %option
# % key: type
# % type: string
# % label: Data type
# % options: thermal_inertia,temperature,albedo,feo,tio2,omat,mineralogy,crust_thickness,gravity_gradient,weh,volatile_proxy,craters,geology_units,custom
# % descriptions: thermal_inertia;Thermal inertia (TIU);temperature;Surface temperature (K);albedo;Surface albedo;feo;FeO abundance (wt-%);tio2;TiO2 abundance (wt-%);omat;Optical maturity index;mineralogy;Generic mineralogy raster;crust_thickness;Crustal thickness (km);gravity_gradient;Gravity gradient (Eotvos);weh;Water equivalent hydrogen (wt-%);volatile_proxy;Generic volatile proxy;craters;Impact crater database CSV;geology_units;Geologic unit vector/raster;custom;User-defined
# % answer: custom
# % required: yes
# %end

# %option
# % key: scale
# % type: double
# % label: Multiplicative scale factor (0 = auto from type defaults)
# % answer: 0
# % required: no
# %end

# %option
# % key: offset
# % type: double
# % label: Additive offset applied after scale
# % answer: 0
# % required: no
# %end

# %option
# % key: resample
# % type: string
# % options: nearest,bilinear,bicubic,bilinear_f,bicubic_f
# % answer: bilinear_f
# % label: Resampling method for reprojection
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % answer: 300
# % label: Maximum memory in MB
# % required: no
# %end

# %flag
# % key: n
# % description: Normalize output to [0,1]
# %end

# %flag
# % key: i
# % description: Invert normalization (high input = low suitability)
# %end

# %flag
# % key: d
# % description: Generate companion crater density raster (type=craters only)
# %end

import os
import sys
import csv
import shutil
import tempfile
import atexit

import grass.script as gs
from grass.exceptions import CalledModuleError

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import normalize_raster

_tmpdir = None


def _cleanup():
    if _tmpdir and os.path.isdir(_tmpdir):
        shutil.rmtree(_tmpdir, ignore_errors=True)


atexit.register(_cleanup)

# ── default scale factors per type ──────────────────────────────────────────
# (km → m conversions where units are km)
_DEFAULT_SCALE = {
    "crust_thickness": 1000.0,   # km → m
    "feo":             1.0,
    "tio2":            1.0,
    "omat":            1.0,
    "thermal_inertia": 1.0,
    "albedo":          1.0,
    "weh":             1.0,
    "temperature":     1.0,
    "gravity_gradient":1.0,
    "custom":          1.0,
}

# Types where high value = WORSE for landing (invert for suitability scoring)
_INVERT_TYPES = {"omat", "thermal_inertia"}   # omat: high=old/safe; but for science high=interesting

# ── crater CSV import ────────────────────────────────────────────────────────

CRATER_COLS = [
    # (zero-based CSV index, GRASS column name, SQL type)
    (0,  "crater_name",   "VARCHAR(80)"),
    (1,  "diameter_km",   "DOUBLE PRECISION"),
    (2,  "lat",           "DOUBLE PRECISION"),
    (4,  "east_lon",      "DOUBLE PRECISION"),
    (6,  "radius_m",      "DOUBLE PRECISION"),
    (12, "depth_km",      "DOUBLE PRECISION"),
    (16, "rim_height_km", "DOUBLE PRECISION"),
    (50, "age",           "VARCHAR(40)"),
    (51, "age_class",     "INTEGER"),
]


def import_craters(csv_path, output, tmpdir, make_density):
    clean_csv = os.path.join(tmpdir, "pin_anc_craters_clean.csv")
    n_rows = 0

    with open(csv_path, newline="", encoding="utf-8-sig") as fin, \
         open(clean_csv, "w", newline="") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        next(reader)  # skip original header

        # clean column names for v.in.ascii
        writer.writerow([c[1] for c in CRATER_COLS])

        for row in reader:
            if len(row) < 52:
                continue
            out = []
            for idx, name, typ in CRATER_COLS:
                val = row[idx].strip() if len(row) > idx else ""
                # convert empty strings to NULL-friendly empty
                out.append(val)
            writer.writerow(out)
            n_rows += 1

    gs.message(f"  Importing {n_rows} craters from CSV…")

    col_def = ",".join(f"{name} {typ}" for _, name, typ in CRATER_COLS)

    gs.run_command(
        "v.in.ascii",
        input=clean_csv,
        output=output,
        format="point",
        separator="comma",
        skip=1,
        x=4,   # east_lon (1-based column in clean CSV)
        y=3,   # lat
        cat=0,
        columns=col_def,
        quiet=True,
        overwrite=gs.overwrite(),
    )

    if make_density:
        den = output + "_density"
        gs.message(f"  Building crater density raster → {den}…")
        tmp_rast = f"pin_anc_cdens_{os.getpid()}"
        gs.run_command("v.to.rast",
                       input=output,
                       output=tmp_rast,
                       use="val",
                       value=1,
                       type="point",
                       quiet=True,
                       overwrite=True)
        gs.run_command("r.null", map=tmp_rast, null=0, quiet=True)
        # 11×11 focal sum as density proxy
        gs.run_command("r.neighbors",
                       input=tmp_rast,
                       output=den,
                       method="sum",
                       size=11,
                       quiet=True,
                       overwrite=gs.overwrite())
        gs.run_command("g.remove", type="raster",
                       name=tmp_rast, flags="f", quiet=True)
        gs.message(f"  Crater density raster: {den}")


# ── generic raster import ────────────────────────────────────────────────────

def import_raster(fpath, output, resample, memory, scale, offset,
                  normalize, invert):
    ext = os.path.splitext(fpath)[1].lower()
    gdal_input = fpath
    if ext in (".img", ".raw"):
        base = os.path.splitext(fpath)[0]
        for suf in (".lbl", ".LBL"):
            if os.path.isfile(base + suf):
                gdal_input = base + suf
                break

    gs.run_command(
        "r.import",
        input=gdal_input,
        output=output,
        resample=resample,
        memory=memory,
        quiet=True,
        overwrite=gs.overwrite(),
    )

    if scale != 1.0 or offset != 0.0:
        tmp = output + "_raw"
        gs.run_command("g.rename", raster=f"{output},{tmp}", quiet=True)
        gs.mapcalc(f"{output} = {tmp} * {scale} + {offset}",
                   overwrite=True, quiet=True)
        gs.run_command("g.remove", type="raster",
                       name=tmp, flags="f", quiet=True)

    if normalize:
        normalize_raster(output, output, invert=invert)


# ── vector import (geology units, custom shapefiles) ────────────────────────

def import_vector(fpath, output):
    gs.run_command(
        "v.import",
        input=fpath,
        output=output,
        quiet=True,
        overwrite=gs.overwrite(),
    )


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    global _tmpdir

    opt_input   = options["input"]
    opt_output  = options["output"]
    opt_type    = options["type"]
    opt_scale   = float(options["scale"])
    opt_offset  = float(options["offset"])
    opt_resamp  = options["resample"]
    opt_memory  = int(options["memory"])
    flag_norm   = flags["n"]
    flag_invert = flags["i"]
    flag_dens   = flags["d"]

    _tmpdir = tempfile.mkdtemp(prefix="pin_anc_")

    input_path = os.path.abspath(opt_input)
    if not os.path.isfile(input_path):
        gs.fatal(f"Input file not found: {input_path}")

    # resolve scale
    if opt_scale == 0.0:
        scale = _DEFAULT_SCALE.get(opt_type, 1.0)
    else:
        scale = opt_scale

    # auto invert for known cost-criteria types (unless user overrides)
    invert = flag_invert

    ext = os.path.splitext(input_path)[1].lower()

    if opt_type == "craters":
        if ext not in (".csv", ".txt"):
            gs.fatal("type=craters requires a CSV input file.")
        import_craters(input_path, opt_output, _tmpdir, flag_dens)

    elif opt_type in ("geology_units",) or ext in (".shp", ".gpkg", ".geojson"):
        import_vector(input_path, opt_output)

    else:
        import_raster(input_path, opt_output, opt_resamp, opt_memory,
                      scale, opt_offset, flag_norm, invert)
        gs.run_command("r.support",
                       map=opt_output,
                       title=f"Planetary ancillary: {opt_type}",
                       history=f"Imported from: {opt_input}",
                       source1="p.in.ancillary",
                       quiet=True)

    gs.message(f"Layer '{opt_output}' ready (type={opt_type}).")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
