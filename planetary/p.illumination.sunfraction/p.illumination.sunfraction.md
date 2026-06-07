## DESCRIPTION

**p.illumination.sunfraction** computes the time-averaged solar illumination
fraction and permanently shadowed region (PSR) mask for any planetary body
over a complete nutation/orbital cycle.

### Methodology (Turchinskaya & Slyuta 2024)

The simulation follows Turchinskaya & Slyuta (2024), who used a 1-hour
timestep over the full lunar nutation cycle (~18.6 years):

1. The nutation cycle is divided into `nsteps` equally-spaced time steps.
2. At each step the sub-solar position (latitude, longitude) is computed from
   the body's orbital and rotational parameters.
3. Solar elevation and azimuth at the region centre are computed analytically
   from the sub-solar position and the region's geographic coordinates. An
   analytic inverse polar stereographic formula is applied for polar
   projected CRS (where projected coordinates are in metres, not degrees).
4. The shadow mask is computed by `sunmask_module` (*p.sunmask* by default,
   using OpenMP+OpenCL acceleration).
5. All per-step masks are averaged with *r.series method=average* to produce
   the illumination fraction.

### Solar elevation formula

For a pixel at geographic coordinates (φ, λ) and sub-solar point (φs, λs):

```
cos(z) = sin(φ)sin(φs) + cos(φ)cos(φs)cos(λ − λs)
elevation = 90° − z
azimuth = atan2(sin(λ − λs), cos(φ)tan(φs) − sin(φ)cos(λ − λs))
```

### Engineering criterion (Turchinskaya & Slyuta 2024)

Luna-27 requires illumination > 35% over the full nutation cycle.
Candidate sites №1 and №2 reach 55–56% average illumination.

### Ephemeris cascade (since v0.6.4)

The sub-solar position is computed via a three-tier cascade with the
`ephemeris=` option:

1. **`spice`** — NAIF SPICE (planetary-cspice + a configured mapset
   meta-kernel via `p.spice.config`). Arcsecond-class accuracy for any
   body whose ephemeris kernels are loaded. Forced with
   `ephemeris=spice`; falls back to Meeus if SPICE is not configured
   under `ephemeris=auto`.

2. **`meeus`** — Self-contained analytic ephemeris (no external
   kernels). For the Moon this is the full Meeus chapter 25+47+53
   libration model (~0.003° latitude). For every other body it is a
   J2000-anchored seasonal model with body-specific Ls calibrations
   (~0.1° latitude). Default under `ephemeris=auto` when SPICE is
   unavailable. The body's J2000 solar longitude is read from
   `body['ls_at_j2000_deg']` if present, else from
   `p_lib._LS_AT_J2000` (Mercury / Venus / Earth / Moon / Mars / Ceres
   / Jupiter / Saturn / Uranus / Neptune / Pluto and the Galilean and
   Saturnian tidally-locked moons).

3. **`analytic`** — Legacy single-sine toy model (kept as a back-compat
   alias). Only invoked when `ephemeris=analytic` is forced; not
   recommended (had a phase-zero bug at non-Moon bodies that put the
   sub-solar latitude near 0° regardless of epoch, breaking
   high-latitude landing-zone illumination).

The catalyst for the v0.6.4 rewrite was the Enceladus south-polar
terrain run at southern summer (2032-01-01) failing with
"Sun never above min_elevation at region centre": the toy ephemeris
gave sub-solar latitudes that never reached the obliquity-driven
±26.7° required to illuminate the south pole. The Meeus path now
correctly returns sub-solar latitude $-26.55°$ at the 2032 southern
solstice, and the SPT pipeline completes 24/24 illumination steps.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `body` | *required* | Body descriptor JSON |
| `nsteps` | 36 | Time steps; 36 for tests, 360 for production, 1000+ to match Turchinskaya & Slyuta 2024 (≈1-h resolution over 18.6-yr nutation cycle) |
| `min_elevation` | 0.0 | Minimum solar elevation to count as illuminated (degrees) |
| `scan_res` | 0 | Resolution in metres for shadow computation. 0 = native DEM resolution. Set to e.g. 30 to coarsen the DEM before per-step shadow casting — speeds up the loop by orders of magnitude and matches the article's working resolution. The computational region is restored on exit. |
| `prefix` | `illum` | Output map name prefix |
| `sunmask_module` | `p.sunmask` | Shadow-mask module to call |

## FLAGS

| Flag | Description |
|------|-------------|
| `-k` | Keep per-timestep mask maps |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_fraction` | Illumination fraction [0–1] |
| `<prefix>_psr_mask` | PSR mask (1=permanently shadowed) |

## EXAMPLES

### Quick test run (36 time steps over nutation cycle)

```bash
# Fast preview: 36 steps ≈ 6 months per step, total ~18.6 years
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=36 prefix=illum_quick

# View results
r.univar map=illum_quick_fraction
r.univar map=illum_quick_psr_mask

# Display illumination fraction (0=dark, 1=always lit)
r.colors map=illum_quick_fraction color=viridis
d.mon start=wx
d.rast map=illum_quick_fraction
```

### Production run (360 time steps, high resolution)

```bash
# Production: 360 steps ≈ 18.6 days per step, ~18.6 year full cycle
# Takes ~6–12 hours on typical hardware (GPU recommended)
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=360 prefix=illum_prod

# Summary statistics
r.univar map=illum_prod_fraction
r.stats illum_prod_fraction columns=3
```

### Article-grade run with coarsened DEM (Turchinskaya & Slyuta 2024 methodology)

The article uses 1-h timesteps (≈163 000 samples) over the full nutation cycle
at 30 m working resolution. A practical approximation:

```bash
# 1000 steps ≈ 6.8 days per step, DEM coarsened to 30 m for speed.
# Result is comparable to the article's reported illumination fractions
# while completing in tens of minutes instead of days.
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=1000 scan_res=30 prefix=illum_art
```

`scan_res=30` triggers a temporary `g.region res=30` plus DEM resampling
(`r.resamp.stats method=average`); both are reverted on exit. The output
maps inherit the *original* region — they are computed at 30 m but kept
in the active region's resolution context for downstream MCDM/ranking.

### Evaluate Luna-27 engineering constraint (≥35% illumination)

```bash
# Run high-resolution illumination
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=360 prefix=illum

# Create binary compliance mask (1 = meets ≥35% requirement)
r.mapcalc "illum_meets_requirement = if(illum_fraction >= 0.35, 1, 0)"

# Find candidate regions
r.univar map=illum_meets_requirement
r.stats -c illum_meets_requirement
```

### Permanent Shadowed Region (PSR) analysis

```bash
# Identify areas never receiving sunlight
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=360 prefix=illum

# PSR mask: 1 = permanently shadowed
r.univar map=illum_psr_mask

# Calculate PSR area (assuming 5m resolution, units are km²)
r.stats -c illum_psr_mask
# PSR_area_km² = N_pixels * 25 / 1e6
```

### Compare accuracy vs. computation time (nsteps trade-off)

```bash
# Quick (36 steps): 15–30 min, lower accuracy
time p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
     nsteps=36 prefix=illum_36

# Production (360 steps): 6–12 hours, high accuracy
time p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
     nsteps=360 prefix=illum_360

# Compare results
r.univar map=illum_36_fraction
r.univar map=illum_360_fraction
```

### Minimum solar elevation criterion

```bash
# Default (0°): all sunlit time
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=360 prefix=illum_0deg min_elevation=0.0

# Strict (5°): only higher elevation angles
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
                           nsteps=360 prefix=illum_5deg min_elevation=5.0

# Compare: illum_5deg_fraction ≤ illum_0deg_fraction everywhere
r.univar map=illum_0deg_fraction
r.univar map=illum_5deg_fraction
```

## PERFORMANCE AND HARDWARE TUNING

The module has three back-ends, picked automatically in this order:

1. **In-RAM + GPU (OpenCL)** — DEM uploaded to the GPU once, one kernel
   launch per time step, mask streamed back. Fastest by an order of magnitude
   when a GPU is available.
2. **In-RAM + CPU/OpenMP** — DEM kept in a NumPy buffer, shadow cast via
   `libpsunmask.so` (`psunmask_cast`) per step. No per-step process fork or
   GRASS raster I/O.
3. **Chunked subprocess** — original path: per step, fork `p.sunmask` (or
   `r.sunmask`), write the mask as a GRASS raster, then aggregate with
   `r.series`. Always available; selected when libpsunmask is missing or
   `-c` is passed.

The first two paths require `libpsunmask.so` (shipped by the
`p-landing-grass` deb at `/usr/local/lib/libpsunmask.so`). The GPU back-end
additionally requires an OpenCL ICD for the device (e.g. `nvidia-opencl-icd`
for NVIDIA, plus a working kernel module).

### Choosing the back-end

| Env var / flag | Effect |
|---|---|
| `SUNMASK_BACKEND=auto` *(default)* | Try GPU; fall back to CPU/OpenMP if unavailable. |
| `SUNMASK_BACKEND=gpu` | Demand GPU; fail loudly if init fails. Useful for catching ICD/driver problems. |
| `SUNMASK_BACKEND=cpu` | Force CPU/OpenMP (hides any OpenCL ICDs for this run). |
| `-c` flag | Force the legacy chunked subprocess path; bypasses libpsunmask entirely. |
| `OMP_NUM_THREADS=N` | CPU thread count for `p.sunmask` / `psunmask_cast`. Match physical cores, **not** SMT logical CPUs. |
| `OMP_PROC_BIND=true OMP_PLACES="{0:N:1}"` | Pin to specific cores — useful on multi-CCD AMD CPUs to keep the working set on one cache domain. |
| `LIBPSUNMASK=/path/to/libpsunmask.so` | Override library lookup. |

The module logs the in-RAM probe and the resulting backend at start-up:

```
In-RAM fast path: ~600 MB working set, 30797 MB available (backend pref=auto).
Loading DEM into RAM (250 MB)…
GPU backend active: Quadro P1000 (3.9 GiB VRAM, 1007 MiB max alloc)
Simulating 1000 steps over 6793.7 days (dt=6.79 d/step, in-RAM)…
... Steps with sun above horizon: 540/1000. (in-RAM, 12345.6 s, 22.86 s/step)
```

### Measured per-step cost (`X s/step` line)

Numbers below are on the Luna-27 polar sector (~62.5 M cells at 30 m,
~3.9 M cells at 120 m) over 200 timesteps. Use them to size a planned run
on your own hardware before committing the publication-grade job.

| Hardware | scan_res | Back-end | s / contributing step | Illumination wall time |
|---|---|---|---|---|
| Intel i7-1165G7, 4 cores, 16 GB | 120 m | CPU/OpenMP (chunked, 8 OMP threads) | 3.5 | ~12 min |
| Intel i7-1165G7, 4 cores, 16 GB | 120 m | CPU/OpenMP (in-RAM, 8 OMP threads) | 5.8 | ~15 min |
| AMD Ryzen 3950X, 16 cores, 32 GB | 120 m | CPU/OpenMP (in-RAM, 16 threads)    | 24    | ~43 min |
| AMD Ryzen 3950X + Quadro P1000 (4 GB) | 120 m | GPU (OpenCL) | **0.34** | **~0:40** |

The GPU path is ~70× faster than 16 CPU threads on this AMD; the CPU result
is bandwidth-limited because shadow casting is memory-bandwidth-bound and
two CCDs contend on dual-channel DDR4.

### Hardware-tuned recipes

Laptop (4 cores, no GPU) — chunked or in-RAM CPU is equivalent:
```bash
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
    nsteps=200 scan_res=120 prefix=illum
```

Workstation with NVIDIA GPU (auto-pick GPU):
```bash
SUNMASK_BACKEND=gpu \
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
    nsteps=1000 scan_res=30 prefix=illum
```

Multi-CCD server, CPU-only (pin to one CCD to avoid cross-die memory traffic):
```bash
OMP_NUM_THREADS=8 OMP_PROC_BIND=true OMP_PLACES="{0:8:1}" \
SUNMASK_BACKEND=cpu \
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
    nsteps=1000 scan_res=30 prefix=illum
```

Diagnose the GPU path (refuse to silently fall back):
```bash
SUNMASK_BACKEND=gpu \
p.illumination.sunfraction dem=lola_5m body=bodies/moon.json \
    nsteps=10 scan_res=120 prefix=test
# If GPU init fails the run aborts with the precise OpenCL error.
```

### When the in-RAM path is automatically declined

The module computes the working-set size (`9 B/cell` heuristic) and skips
the in-RAM path if `MemAvailable < 1.5 × working set`. The chunked
subprocess path is then used. To force the chunked path regardless, pass
`-c`. To override the heuristic, free RAM or override the DEM resolution
via `scan_res`.

## NOTES

For the Moon, the full nutation cycle is 18.61 years; `nsteps=8766` (one step per hour) reproduces the Turchinskaya & Slyuta (2024) benchmark. Use `nsteps=360` for a quick preview at 1° orbital resolution. The output PSR mask is 1 where the illumination fraction equals zero (permanently shadowed region) and 0 elsewhere.

## SEE ALSO

*[p.sunmask](p.sunmask.md),
[p.illumination.shadow](p.illumination.shadow.md),
[r.sunmask](https://grass.osgeo.org/grass-stable/manuals/r.sunmask.html),
[r.series](https://grass.osgeo.org/grass-stable/manuals/r.series.html)*

## REFERENCES

- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011
  *(1-h timestep, full nutation-cycle simulation, illumination methodology)*
- Noda, H. et al. (2008) Illumination conditions at the lunar polar regions
  by KAGUYA (SELENE) laser altimeter. *Geophysical Research Letters* 35,
  L24203.

## AUTHOR

Yann Chemin
