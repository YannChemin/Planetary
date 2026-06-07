## NAME

**p.landing.qt** - Planetary landing-site evaluation wizard (Qt6 standalone GUI)

## KEYWORDS

Planetary, Landing Pipeline, Qt6, wizard, GUI

## SYNOPSIS

```
python3 p_landing_qt.py [--session FILE]
```

## DESCRIPTION

**p.landing.qt** is a Qt6-native standalone wizard that guides the user
through the full planetary landing-site evaluation pipeline step by step,
running the underlying `p.*` GRASS modules in background threads and
streaming their output to an integrated log panel.

The wizard consists of seven sequential pages:

| Step | Page | Modules invoked |
|------|------|-----------------|
| 1 | Setup | *(configuration only)* |
| 2 | DEM Import | `p.in.pds` |
| 3 | Terrain Analysis | `p.terrain.slope`, `p.terrain.roughness`, `p.terrain.hazard`, `p.terrain.ellipse` |
| 4 | Illumination | `p.illumination.sunfraction`, `p.illumination.shadow` (using `p.sunmask`) |
| 5 | Visibility | `p.visibility.earth`, `p.visibility.los` |
| 6 | MCDM Scoring | `p.mcdm.score` (WLC and/or TOPSIS) |
| 7 | Ranking & Results | `p.rank` — candidate table + JSON report |

Each page must complete successfully before the wizard advances. Parameters
are persisted automatically to a JSON session file
(`~/.p_landing_wizard_session.json` by default) so a run can be resumed
after interruption.

### Step 1 — Setup

Select the GRASS database directory, location name, mapset, body descriptor
JSON (e.g. `bodies/moon.json`) and mission configuration JSON
(e.g. `missions/luna27.json`).

### Step 2 — DEM Import

Browse to a PDS3 label, GeoTIFF, ISIS cube or other supported format and
specify the output GRASS raster map name. `p.in.pds` auto-detects the
format.

### Step 3 — Terrain Analysis

Configure multi-scale slope (comma-separated scales in metres and per-scale
degree thresholds), RMS roughness window and threshold, and landing-ellipse
major/minor axes and scan resolution. The four terrain modules run in
sequence; each sub-step is logged separately.

### Step 4 — Illumination Analysis

Set the number of time-steps for the solar-illumination integration, choose
between `p.sunmask` (OpenCL + OpenMP, fast) and `r.sunmask` (serial), and
set the shadow-hazard fractional threshold. Produces an illumination
fraction raster and a PSR mask.

### Step 5 — Visibility Analysis

Configure Earth-visibility parameters (number of epochs, minimum Earth
elevation angle) and horizon-masking parameters (angular step, scan
resolution, number of LOS directions). Produces Earth-visibility fraction
and line-of-sight horizon rasters.

### Step 6 — MCDM Scoring

Enter criterion weights (slope, roughness, illumination, Earth visibility,
science) that must sum to 1.0; the wizard normalises automatically if they
do not. Select WLC only, TOPSIS only, or both. Produces a `suitability_wlc`
raster (and optionally `suitability_topsis`).

### Step 7 — Ranking & Results

Set the minimum candidate area (km²), suitability percentile threshold,
maximum number of candidates, and number of Monte Carlo weight-perturbation
samples. After `p.rank` completes, the ranked candidates are displayed in
a table showing mean suitability, standard deviation, area and
Monte-Carlo rank-1 probability. The full report is written to a JSON file.

## REQUIREMENTS

- PyQt6 (`pip install PyQt6`)
- GRASS GIS 8.x in `$PATH` or `$GISBASE` set, with the `p.*` Planetary
  addon suite installed

## NOTES

`p.landing.qt` is a standalone application: it does not need to be launched
from inside a GRASS session. It locates GRASS automatically via `$GISBASE`,
`grass --config path`, or common installation prefixes
(`/usr/local/grass86`, `/usr/lib/grass86`, …).

For a wxPython wizard integrated into the GRASS GUI see `g.gui.landing`.
For a non-interactive batch pipeline see `p.landing`.

## EXAMPLES

Launch the wizard using the default session file:

```
python3 p_landing_qt.py
```

Resume a previous session saved to a custom path:

```
python3 p_landing_qt.py --session /data/luna27/wizard_session.json
```
## SEE ALSO

*[g.gui.landing](g.gui.landing.md),
[p.landing](p.landing.md),
[p.terrain.slope](p.terrain.slope.md),
[p.terrain.roughness](p.terrain.roughness.md),
[p.terrain.hazard](p.terrain.hazard.md),
[p.terrain.ellipse](p.terrain.ellipse.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.illumination.shadow](p.illumination.shadow.md),
[p.sunmask](p.sunmask.md),
[p.visibility.earth](p.visibility.earth.md),
[p.visibility.los](p.visibility.los.md),
[p.mcdm.score](p.mcdm.score.md),
[p.rank](p.rank.md)*

## AUTHOR

Yann Chemin
