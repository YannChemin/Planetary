## DESCRIPTION

**p.landing** is the master pipeline for planetary landing-site evaluation.
It chains all `p.*` modules in order, reads a JSON state file to skip
completed stages on re-runs, and writes a summary JSON report.

### Pipeline stages

| Stage | Modules | Description |
|-------|---------|-------------|
| `terrain` | p.terrain.slope, p.terrain.roughness, p.terrain.hazard, p.terrain.ellipse | Multi-scale slope, roughness, hazard composite, ellipse scan |
| `illumination` | p.illumination.sunfraction, p.illumination.shadow | Time-averaged illumination fraction and shadow frequency |
| `visibility` | p.visibility.earth, p.visibility.los, p.visibility.orbiter | Earth/relay visibility, horizon masking, orbiter contact |
| `mcdm` | p.mcdm.weight, p.mcdm.score | AHP weights, WLC+TOPSIS suitability scoring |
| `rank` | p.rank | Candidate extraction, ranking, Monte Carlo sensitivity |

Stages can be selected or skipped via `stages=` and `skip=`. A JSON state
file records completed stages for resumable runs.

### Body and mission JSON

The `body=` JSON provides planetary physical constants: radius, GM, solar
day, nutation period, orbital elements. The `mission=` JSON controls
mission-specific parameters: ellipse size, slope/roughness thresholds,
criterion weights, and science target priorities.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres, already in GRASS mapset) |
| `body` | *required* | Body descriptor JSON file |
| `mission` | *required* | Mission configuration JSON file |
| `stages` | `terrain,illumination,visibility,mcdm,rank` | Comma-separated stages to run |
| `skip` | — | Comma-separated stages to skip |
| `ancillary` | — | JSON mapping: ancillary layer name → GRASS raster map name |
| `state` | `.p_landing_state.json` | JSON state file tracking completed stages |
| `report` | `landing_report.json` | Output JSON summary report |
| `nprocs` | `4` | OpenMP threads forwarded to terrain (*r.slope.aspect*, *r.neighbors*) and visibility (*r.horizon*). Mission JSON `"nprocs"` overrides; explicit values pass through verbatim (no clamping), so a fast-preview config can deliberately undercut the default. |
| `memory` | `3000` | Row-cache size (MB) forwarded to the same modules. Mission JSON `"memory"` overrides. |

## FLAGS

| Flag | Description |
|------|-------------|
| `-c` | Clean up all temporary maps on completion |
| `-f` | Force re-run of all stages (ignore state file) |

## BODY JSON SCHEMA

The body JSON describes planetary physical constants. All fields are required unless otherwise noted.

| Field | Type | Example (Moon) | Description |
|-------|------|---|---|
| `name` | string | Moon | Body name for display and logging |
| `isis_target` | string | MOON | ISIS3 target name (used by p.in.pds, p.in.dem, p.in.ancillary) |
| `semi_major_axis_m` | number | 1737400 | Semi-major axis in metres (for ellipsoid if not sphere) |
| `semi_minor_axis_m` | number | 1737400 | Semi-minor axis in metres; equal to semi_major_axis for sphere |
| `ellipsoid` | string | sphere | Either "sphere" or "ellipsoid"; determines proj4 reference |
| `gravity_ms2` | number | 1.62 | Surface gravity in m/s² (informational) |
| `solar_day_hours` | number | 708.7 | Length of one full solar day in hours |
| `sidereal_period_days` | number | 27.32 | Sidereal rotation period in days (for p.illumination.sunfraction internal checks) |
| `synodic_period_days` | number | 29.53 | Synodic period (lunar month) in days; used for illumination baseline |
| `axial_tilt_deg` | number | 1.54 | Axial tilt relative to orbital plane in degrees |
| `orbital_inclination_deg` | number | 5.14 | Orbital inclination in degrees (affects Earth visibility) |
| `nutation_period_years` | number | 18.6 | Full nutation cycle in years; p.illumination.sunfraction samples full cycle |
| `solar_constant_Wm2` | number | 1361.0 | Solar constant at body's distance from Sun in W/m² |
| `atmosphere` | boolean | false | Whether body has significant atmosphere (affects visibility) |
| `proj4_longlat` | string | +proj=longlat +a=1737400 ... | Proj4 string for lon/lat projection |
| `proj4_south_stereo` | string | +proj=stere +lat_0=-90 ... | Proj4 string for south polar stereographic |
| `proj4_north_stereo` | string | +proj=stere +lat_0=90 ... | Proj4 string for north polar stereographic |
| `notes` | string | Reference radius... | Free-text notes (optional) |

### Complete Moon body.json example

```json
{
  "name": "Moon",
  "isis_target": "MOON",
  "semi_major_axis_m": 1737400,
  "semi_minor_axis_m": 1737400,
  "ellipsoid": "sphere",
  "gravity_ms2": 1.62,
  "solar_day_hours": 708.7,
  "sidereal_period_days": 27.32,
  "synodic_period_days": 29.53,
  "axial_tilt_deg": 1.54,
  "orbital_inclination_deg": 5.14,
  "nutation_period_years": 18.6,
  "solar_constant_Wm2": 1361.0,
  "atmosphere": false,
  "proj4_longlat": "+proj=longlat +a=1737400 +b=1737400 +no_defs",
  "proj4_south_stereo": "+proj=stere +lat_0=-90 +lat_ts=-90 +lon_0=0 +a=1737400 +b=1737400 +units=m +no_defs",
  "proj4_north_stereo": "+proj=stere +lat_0=90 +lat_ts=90 +lon_0=0 +a=1737400 +b=1737400 +units=m +no_defs",
  "notes": "Reference radius 1737.4 km. DE421 mean Earth/polar axis frame."
}
```

## MISSION JSON SCHEMA

The mission JSON controls all mission-specific parameters: landing ellipse size, slope/roughness thresholds at multiple scales, visibility criteria, and AHP weights for MCDM. All fields except `notes` are required.

| Field | Type | Example (Luna-27) | Description |
|-------|------|---|---|
| `mission` | string | Luna-27 | Mission name for display and reporting |
| `body` | string | Moon | Must match the `name` field of the body JSON |
| `lander_type` | string | soft_lander | Type of lander (soft_lander, rover, sample_return, etc.) |
| `ellipse_major_m` | number | 30000 | Semi-major axis of landing ellipse in metres |
| `ellipse_minor_m` | number | 15000 | Semi-minor axis of landing ellipse in metres |
| `slope_thresholds_deg` | object | {"30": 10, "100": 7, ...} | Max acceptable slope at each scale (m) — keys are scale (metres), values are thresholds (degrees) |
| `roughness_rms_max_m` | number | 0.5 | Maximum acceptable RMS height variation in metres |
| `min_illumination_fraction` | number | 0.35 | Minimum time-averaged solar illumination fraction (0.0–1.0) |
| `earth_visibility_min_fraction` | number | 0.50 | Minimum time-averaged Earth visibility fraction (0.0–1.0) |
| `earth_elevation_min_deg` | number | 3.0 | Minimum Earth elevation angle above horizon in degrees |
| `orbiter_altitude_km` | number | 100 | Orbital altitude of relay orbiter in kilometres |
| `orbiter_inclination_deg` | number | 90 | Orbital inclination of relay orbiter in degrees |
| `science_targets` | array | ["volatile_ice", "weh", "geology"] | Science priorities (mapped to ancillary rasters if provided) |
| `criteria_weights` | object | {"slope": 0.25, ...} | AHP weights for each criterion (must sum to 1.0): slope, roughness, illumination, earth_vis, science |
| `hard_exclusion` | object | {"slope_max_deg": 15, ...} | Hard thresholds that exclude pixels regardless of MCDM score |
| `top_percentile` | number | 70.0 | Suitability percentile threshold used by `p.rank` to extract candidate clumps. Lower = looser. Default 70 if absent. |
| `scan_res` | number | 30 | Working resolution in metres for `p.illumination.sunfraction` and `p.visibility.earth`. 0 / absent = native DEM resolution. Use 30 to match the Turchinskaya & Slyuta 2024 methodology. |
| `illum_nsteps` | number | 1000 | Number of timesteps for the illumination simulation over the nutation cycle. 36 for tests; 1000+ for article-grade results. |
| `vis_nsteps` | number | 1000 | Number of timesteps for the Earth-visibility simulation. Same scale guidance as `illum_nsteps`. |
| `region_bounds` | object | {"n":..,"s":..,"e":..,"w":..,"res":..} | Optional. If present, p.landing calls `g.region` with these bounds before any stage runs and then clips the region to the DEM's data extent (warning if it had to clip; fatal if no intersection). Units must match the current location CRS (metres for projected, degrees for longlat). When the bounds object has no `res`/`nsres`/`ewres` key, the active region resolution falls back to `scan_res` (since v0.6.3). If `region_bounds` is **absent**, p.landing aligns the active region to the DEM's full extent at `scan_res` resolution (since v0.6.3) — saving the terrain stage from running at native posting on high-resolution DEMs (a 1 m HiRISE DEM with `scan_res=2` previously yielded 97 M cells at native, now 25 M). With no `scan_res` either, the region falls back to the DEM's native posting. |
| `study_area_note` | string | "The bundled run uses..." | Optional free-text disclaimer about deviations from the source article (e.g. when the DEM does not cover the article's target sector). Surfaces in `p.landing` log and final JSON report. |
| `notes` | string | Per Turchinskaya... | Free-text notes on mission objectives (optional) |

### Complete Luna-27 mission.json example

```json
{
  "mission": "Luna-27",
  "body": "Moon",
  "lander_type": "soft_lander",
  "ellipse_major_m": 30000,
  "ellipse_minor_m": 15000,
  "top_percentile": 70.0,
  "scan_res": 30,
  "illum_nsteps": 1000,
  "vis_nsteps": 1000,
  "region_bounds": {
    "n": -133748,
    "s": -334534,
    "e":  260022,
    "w":   -5839,
    "res": 30,
    "_comment": "Lunar south-polar stereographic metres (sphere R=1737400, lat_0=-90, lon_0=0). Covers Turchinskaya & Slyuta 2024 sector 51E-1W, 83-79S."
  },
  "slope_thresholds_deg": {
    "30":   10,
    "100":   7,
    "1000":  5,
    "10000": 3
  },
  "roughness_rms_max_m": 0.5,
  "min_illumination_fraction": 0.35,
  "earth_visibility_min_fraction": 0.50,
  "earth_elevation_min_deg": 3.0,
  "orbiter_altitude_km": 100,
  "orbiter_inclination_deg": 90,
  "science_targets": ["volatile_ice", "weh", "geology"],
  "criteria_weights": {
    "slope":          0.25,
    "roughness":      0.15,
    "illumination":   0.20,
    "earth_vis":      0.15,
    "science":        0.25
  },
  "hard_exclusion": {
    "slope_max_deg":        15,
    "roughness_rms_max_m":   1.0,
    "illumination_min":      0.0,
    "earth_vis_min":         0.0
  },
  "notes": "Per Turchinskaya & Slyuta 2024. Polar sector 51E-1W, 83-79S. 2028 planned landing."
}
```

## GRACEFUL FAILURE AND REPORTING

p.landing wraps every stage with a per-stage error handler. A failure in
one stage does **not** abort the pipeline; instead:

1. The error type, message, and full module stderr are captured.
2. Downstream stages that strictly depend on the failed stage are skipped
   with a clear warning (e.g. `mcdm` is skipped if `terrain` failed).
3. The final JSON report includes a `stage_status` map:

```json
{
  "status": "completed_with_errors (1 stage(s) failed)",
  "stage_status": {
    "terrain":      {"ok": true,  "skipped": false},
    "illumination": {"ok": true,  "skipped": false},
    "visibility":   {"ok": true,  "skipped": false},
    "mcdm":         {"ok": true,  "skipped": false},
    "rank": {
      "ok": false,
      "error_type": "CalledModuleError",
      "error": "Module run `p.rank ...` ended with an error.",
      "stderr": "No candidate regions met the minimum area threshold..."
    }
  },
  "terrain_outputs":     {...},
  "suitability_outputs": {...}
}
```

`status` is `"ok"` when every stage succeeded or was skipped, and
`"completed_with_errors (N stage(s) failed)"` otherwise. The exit code is
always 0 — wrappers (wx, qt, CI) should branch on the JSON `status` field.

## EXAMPLES

### Full Luna-27 evaluation pipeline (south polar)

```bash
# Import source data first
g.region raster=lola_5m -p

# Run complete evaluation with all stages
p.landing dem=lola_5m body=bodies/moon.json mission=missions/luna27.json \
          report=luna27_report.json -c

# Check results
g.list type=raster pattern="*suitability*"
cat luna27_report.json
```

### Multi-mission comparison (Luna-27 vs Artemis)

```bash
# Evaluate both missions on same DEM
p.landing dem=lola_5m body=bodies/moon.json mission=missions/luna27.json \
          report=luna27_report.json -f
p.landing dem=lola_5m body=bodies/moon.json mission=missions/artemis.json \
          report=artemis_report.json -f

# Compare suitability maps side-by-side
d.mon start=wx
d.rast map=suitability_luna27
d.rast map=suitability_artemis
```

### Artemis-III multi-region study: sourcing high-resolution DEMs

The Artemis-III evaluation defined in `config/artemis/*.json` runs
*p.landing* over 9 candidate south-polar regions. DEM coverage is
heterogeneous:

- Six near-pole sites (lat < -87.5°: Shackleton, Connecting Ridge,
  de Gerlache rims, Haworth) fall inside the LOLA polar 5 m cap
  (`ldem_875s_5m_float`).
- Two mid-polar sites (Nobile Rim 1 at -85.30°, Malapert Massif at
  -85.99°) need the LOLA 20 m polar cap (`ldem_85s_20m`).
- Mons Mouton (-84.60°) is outside the 85°S cap and needs the 30 m
  product (`ldem_75s_30m_float`).

For a subset of these sites, higher-resolution **LROC NAC stereo DTMs**
(~2–5 m posting) are publicly available from the ASU LROC PDS archive.
The `p.in.lroc.nac` addon (from the `grass-planetary-addons` package)
discovers and fetches them:

```bash
# One-shot: build a local lat/lon index of all ~660 NAC DTMs (~30 s)
p.in.lroc.nac -r

# List south-polar candidates and check which Artemis sites they cover
p.in.lroc.nac bbox=0,-90,360,-80 -l limit=50
```

Out of the 9 Artemis sites, only 2 have direct NAC coverage
(`connecting_ridge` → `ESALL_CR1`; `malapert_massif` → `MALAPERT02` /
`MALAPERT03`), plus 1 useful sensitivity product (`ESALL_SR12`,
Shackleton rim). Stage the four relevant products on the workstation
and sync to the compute server:

```bash
# Stage ~120 MB of NAC DTM tiles locally without importing.
# Use the standard $HOME/RSDATA/Moon/ tree so NAC sits next to the
# LOLA polar caps (LOLAPOLARDEM/) under one canonical data root.
NAC=$HOME/RSDATA/Moon/NAC
p.in.lroc.nac name=ESALL_CR1   -d download_dir=$NAC
p.in.lroc.nac name=ESALL_SR12  -d download_dir=$NAC
p.in.lroc.nac name=MALAPERT02  -d download_dir=$NAC
p.in.lroc.nac name=MALAPERT03  -d download_dir=$NAC

rsync -av $NAC/  server:RSDATA/Moon/NAC/

# On the server, in a south-polar-stereographic GRASS project, import:
r.in.gdal input=$HOME/RSDATA/Moon/NAC/MALAPERT02/NAC_DTM_MALAPERT02.TIF \
          output=nac_malapert02_dtm

# Then point a per-region Artemis mission JSON at it:
#   "input_dem": "nac_malapert02_dtm",
#   "region_bounds": { "n": ..., "s": ..., "e": ..., "w": ..., "res": 5 }
p.landing dem=nac_malapert02_dtm body=bodies/moon.json \
          mission=config/artemis/malapert_massif_nac.json \
          report=malapert_nac_report.json -f
```

The remaining 7 Artemis sites stay on LOLA at their native posting
(5/20/30 m). The article frames this heterogeneity as a real-world
constraint and uses the NAC sites for a **resolution-sensitivity
sub-study** (e.g. LOLA 5 m vs NAC ~2 m on `connecting_ridge`: do the
candidate clusters and rank order persist?).

### Run terrain and illumination stages only (quick preview)

```bash
p.landing dem=lola_5m body=bodies/moon.json mission=missions/luna27.json \
          stages=terrain,illumination report=quick_preview.json
```

### Resume from a previous run (skipping completed expensive stages)

```bash
# First run: terrain and illumination (2 hours)
p.landing dem=lola_5m body=bodies/moon.json mission=missions/luna27.json \
          stages=terrain,illumination state=.landing_state.json

# Later: add visibility, MCDM, ranking without re-computing terrain/illumination
p.landing dem=lola_5m body=bodies/moon.json mission=missions/luna27.json \
          state=.landing_state.json report=full_report.json

# View state
cat .landing_state.json
```

### Custom criteria weights (emphasis on science targets)

```bash
# Create modified mission JSON with higher science weight
cat > custom_mission.json << EOF
{
  "mission": "Luna-27-Science-Focus",
  "body": "Moon",
  "lander_type": "soft_lander",
  "ellipse_major_m": 30000,
  "ellipse_minor_m": 15000,
  "slope_thresholds_deg": {"30": 10, "100": 7, "1000": 5, "10000": 3},
  "roughness_rms_max_m": 0.5,
  "min_illumination_fraction": 0.35,
  "earth_visibility_min_fraction": 0.50,
  "earth_elevation_min_deg": 3.0,
  "orbiter_altitude_km": 100,
  "orbiter_inclination_deg": 90,
  "science_targets": ["volatile_ice"],
  "criteria_weights": {
    "slope":          0.15,
    "roughness":      0.10,
    "illumination":   0.10,
    "earth_vis":      0.10,
    "science":        0.55
  },
  "hard_exclusion": {
    "slope_max_deg": 15,
    "roughness_rms_max_m": 1.0,
    "illumination_min": 0.0,
    "earth_vis_min": 0.0
  }
}
EOF

p.landing dem=lola_5m body=bodies/moon.json mission=custom_mission.json \
          report=science_focused_report.json -c
```

### Output report interpretation

```bash
# The landing_report.json contains:
# - stage: "rank" (final stage completed)
# - candidates: array of ranked landing sites with:
#   - centroid_lat, centroid_lon: site location
#   - mean_suitability: (0.0–1.0) overall suitability score
#   - area_km2: size of candidate landing ellipse
#   - robustness: sensitivity to weight perturbations (higher = more robust)
#   - terrain_score, illumination_score, visibility_score, science_score

# Example extraction:
jq '.candidates[0]' landing_report.json
jq '.candidates[] | select(.mean_suitability > 0.7)' landing_report.json
```

## PERFORMANCE AND HARDWARE TUNING

`p.landing` forwards two CLI options to its sub-modules so a single command
can drive every parallelisable step at the right scale for the host:

- `nprocs=N` → reaches *r.slope.aspect* and *r.neighbors* (terrain) and
  *r.horizon* (visibility) via *p.terrain.\** and *p.visibility.earth*.
- `memory=MB` → row-cache size for *r.slope.aspect* / *r.neighbors*
  (`r.horizon` ignores `memory=` in current GRASS but accepts the keyword).

If the mission JSON sets `"nprocs"` or `"memory"`, those values override the
CLI defaults verbatim — no clamping — so a `_fast.json` preview config can
deliberately undercut the workstation defaults.

The illumination stage uses **`p.illumination.sunfraction`**, which has its
own GPU/CPU back-end selector controlled by the `SUNMASK_BACKEND`
environment variable (see that module's manual for the full table). The
variable is read by `p.landing`'s child process automatically; you just
export it before invoking `p.landing`.

### Hardware-tuned recipes

Laptop (4–8 cores, no GPU): use sensible defaults.
```bash
p.landing dem=lola_30m body=bodies/moon.json mission=missions/luna27.json \
    nprocs=4 memory=2000 report=report.json
```

Workstation server, **no GPU**, single-CCD pinning on a multi-CCD AMD:
```bash
OMP_NUM_THREADS=8 OMP_PROC_BIND=true OMP_PLACES="{0:8:1}" \
SUNMASK_BACKEND=cpu \
p.landing dem=lola_30m body=bodies/moon.json mission=missions/luna27.json \
    nprocs=16 memory=6000 report=report.json
```

Workstation server **with NVIDIA GPU** — the headline configuration:
```bash
OMP_NUM_THREADS=16 SUNMASK_BACKEND=gpu \
p.landing dem=lola_30m body=bodies/moon.json mission=missions/luna27.json \
    nprocs=16 memory=6000 report=report.json
```

### Measured impact (Luna-27 sector, preview run: 200 steps, 120 m)

| Host | Illumination | Visibility | Terrain | Notes |
|---|---|---|---|---|
| Laptop (i7-1165G7, 4c/8t, 16 GB) | 11:46 | 18:53 | 3:15 | Chunked subprocess, `nprocs=1` defaults. |
| AMD 3950X (16c/32t), 32 GB | 43:05 | 19:14 | 3:01 | CPU/OMP, 16 threads on 2 CCDs (bandwidth-bound). |
| AMD 3950X + Quadro P1000 4 GB | **0:40** | ~19:00 | 3:01 | GPU back-end via `SUNMASK_BACKEND=gpu`. |

Extrapolated to the publication-grade run (30 m, 1000 steps):

| Host | Total wall time |
|---|---|
| Laptop (i7-1165G7) | ~3.5–5 days |
| AMD 3950X CPU only | ~18 h–4 days (sensitive to thread/CCD tuning) |
| AMD 3950X + Quadro P1000 | **~5–8 h end-to-end** |

The GPU back-end collapses illumination from the dominant cost to a
near-incidental one; visibility, terrain and ranking then become the
remaining ~1 h.

### Diagnosing back-end selection in a `p.landing` run

Watch the illumination stage log:
```
[2/5] illumination
  ▸ p.illumination.sunfraction (nsteps=1000, ephemeris=auto, scan_res=30 m)
  In-RAM fast path: ~600 MB working set, 30440 MB available (backend pref=auto).
  Loading DEM into RAM (250 MB)…
  GPU backend active: Quadro P1000 (3.9 GiB VRAM, 1007 MiB max alloc)
  Simulating 1000 steps … (in-RAM, T s, X s/step)
```

If you see `GPU unavailable (no OpenCL platform)`, an OpenCL ICD isn't
visible — either install `nvidia-opencl-icd` (and ensure the kernel module
is loaded), or stop hiding ICDs (e.g. another script may have set
`OCL_ICD_VENDORS=/nonexistent` to force CPU mode).

If you see `GPU unavailable (no OpenCL GPU device)`, the loader sees a
platform but no GPU device — usually nouveau holding the card, or the
proprietary kernel module not loaded for the running kernel
(`dkms status` / `lsmod | grep nvidia`).

## END-TO-END SERVER RERUN RECIPE

The single shell sequence below reproduces a full Luna-27-class
analysis from scratch on a workstation-class server with a
CUDA-capable GPU. It (a) wipes any prior state file and derivative
rasters so every stage re-executes from the input DEM, (b) launches
`p.landing` detached via `nohup setsid` so the run survives logout, and
(c) tunes runtime parameters via environment variables: `P_HORIZON_TIMEOUT`
bounds `r.horizon` per azimuth (default is too short for 60 M-cell polar
grids), `SUNMASK_BACKEND=gpu` steers `p.sunmask` to its OpenCL path,
`THREADS` caps OpenMP threads to physical cores, and `MEMORY_MB` sets the
row-cache for `r.slope.aspect`, `r.neighbors` and `r.horizon`.

```bash
# 1. wipe state + all derivative maps so every stage re-runs from scratch
rm -f ~/grassdata/Moon_SouthPole_5m/luna27_article_state.json \
      ~/grassdata/Moon_SouthPole_5m/luna27_article_report.json

grass ~/grassdata/Moon_SouthPole_5m/luna27_article --exec sh -c '
  for p in "pterr_*" "pillum_*" "pvis_*" "pmcdm_*" "prank_*" \
           "slope_*" "roughness_*" "hazard_*" "illum_*" \
           "earth_vis_*" "suitability_*" "rank_*"; do
    g.remove -f type=raster pattern="$p" 2>/dev/null
  done
  g.remove -f type=vector pattern="rank_*" 2>/dev/null
'

# 2. detached full run: GPU shadows + 16-thread r.horizon + 6 GB row cache
P_HORIZON_TIMEOUT=20000 SUNMASK_BACKEND=gpu THREADS=16 MEMORY_MB=6000 \
nohup setsid sh ~/grassdata/Moon_SouthPole_5m/article/scripts/run_experiment_server.sh \
  > ~/full_run_v037.log 2>&1 &
echo "PID=$!"
```

Monitor with `tail -f ~/full_run_v037.log` (Ctrl-C detaches, the job keeps
running) and `ps -fp $(pgrep -f p.landing)`. Expected wall time on a
16-core workstation with a Quadro P1000-class GPU: terrain ~3 min,
illumination ~3 h, visibility ~2.5 h, mcdm seconds, rank ~10 s — about
six hours end to end.

## NOTES

Re-running with the same state file resumes from the last completed stage; set `force=yes` to recompute a stage whose output rasters already exist. The JSON state/report format is versioned — do not hand-edit it. The report written at the end is the same schema consumed by *p.rank.cross* for multi-region cross-comparison.

## SEE ALSO

*[p.terrain.slope](p.terrain.slope.md),
[p.terrain.roughness](p.terrain.roughness.md),
[p.terrain.hazard](p.terrain.hazard.md),
[p.terrain.ellipse](p.terrain.ellipse.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.illumination.shadow](p.illumination.shadow.md),
[p.visibility.earth](p.visibility.earth.md),
[p.visibility.los](p.visibility.los.md),
[p.visibility.orbiter](p.visibility.orbiter.md),
[p.mcdm.weight](p.mcdm.weight.md),
[p.mcdm.score](p.mcdm.score.md),
[p.rank](p.rank.md),
[g.gui.landing](g.gui.landing.md),
[p.in.lroc.nac](p.in.lroc.nac.md)*

## REFERENCES

- Golombek, M.P. et al. (2003) Selection of the Mars Exploration Rover
  landing sites. *Journal of Geophysical Research: Planets* 108(E12), 8072.
  doi:10.1029/2003JE002074
- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window.
  *Remote Sensing* 15, 3184. doi:10.3390/rs15123184
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
