## DESCRIPTION

*p.horizon.gpu* computes per-pixel horizon elevation rasters from a DEM,
one raster per requested azimuth. It is a CLI-compatible accelerator for
*r.horizon*'s most common use case &mdash; precomputing a fixed set of
azimuths for later interpolation &mdash; and produces the same filename
convention (`basename_NNN_F`), so it is a drop-in for
`p_lib.precompute_horizons` without code changes downstream.

Ray-marching runs on an OpenCL device (auto-detected GPU, or CPU device
via PoCL) and falls back to OpenMP if no OpenCL platform is available.
Use the **-c** flag to force the OpenMP backend.

**Numerics are NOT bit-comparable to r.horizon.** *r.horizon*'s direction
estimator perturbs lat/lon by 0.0001&nbsp;rad (&asymp;&nbsp;174&nbsp;km on
the lunar pole), then re-projects to derive a unit walk direction; the
resulting chord-vs-tangent error drifts the ray laterally by metres to
hundreds of metres on polar projections. *p.horizon.gpu* uses a per-pixel
local-tangent rotation plane computed by perturbing longitude by 1e-4°
in projected coords, giving the geometrically correct geographic walk
direction. On polar DEMs the two implementations agree on east/west
azimuths within 0.1° mean and disagree by up to a few degrees on
north/south azimuths &mdash; in favour of this module.

### Algorithm

1. Pre-compute one rotation angle per DEM pixel: the angle from
   projected +x to the local geographic-east tangent, via
   `PJ_FWD → +ε lon → PJ_INV`.
2. Upload the DEM as an OpenCL `image2d_t` (CL_R/CL_FLOAT, nearest
   sampling) and the rotation plane as a float buffer.
3. For each requested azimuth (CCW from geographic east, degrees),
   launch one kernel: each work-item walks its ray cell-by-cell at
   half-cell sub-cell steps, samples the DEM at cell centres, applies
   curvature correction `−d²/(2R)`, and reduces to max horizon
   elevation.
4. Convert to degrees and write one output raster per azimuth.

### Supported projections (CRS guard)

The kernel needs to know how a projected step (Δx, Δy) maps to a true
geographic step in metres so that the ray-distance cap, the curvature
term &minus;d²/(2R), and the horizon angle atan2(dz, d) are all
geometrically faithful. Two regimes are supported:

**Conformal projections** — east and north are locally orthogonal AND
the local metric is isotropic (cell size encodes the local metre).
Handled with a single per-pixel rotation angle that maps projected +x to
local geographic east; per-row metric factors are 1.0. Accepted PROJ
identifiers:

- `stere`, `sterea`, `ups` — Stereographic / Universal Polar Stereographic
- `merc` — Mercator
- `tmerc`, `etmerc`, `utm` — (Extended) Transverse Mercator / UTM
- `lcc` — Lambert Conformal Conic
- `omerc`, `somerc`, `gstmerc` — Oblique Mercator variants

**Cylindrical anisotropic projections** (since v0.6.3) — east and north
are still orthogonal in projected coords but the metric is
row-dependent. Handled with NO rotation and per-row metric factors
(`metric_x[row]`, `metric_y[row]`) computed via PJ_FWD/PJ_INV
perturbation along the central column. Accepted PROJ identifiers:

- `eqc` — Equirectangular / Plate&nbsp;Carrée (most planetary mosaics,
  e.g. Mars MOLA global, HiRISE-derived DTMs, Mars 2015 Sphere products,
  Bland-2022 Europa Galileo SSI DTMs, Dawn Ceres HAMO global DTM)
- `cea` — Lambert Cylindrical Equal Area

For vanilla `eqc` (`lat_ts=0`) the metric reduces to
`metric_x[row] = cos(latitude_at_row)` and `metric_y[row] = 1.0`, i.e.
the well-known cos(φ) latitude correction. For non-standard
`lat_ts` and for `cea`, the perturbation method recovers the exact
metric automatically. At polar latitudes the metric is clamped at 0.1
(beyond ~84°) to avoid pathological ray envelopes; the module will
issue a warning if the AOI's DEM straddles such a clamped region.

Lat/lon locations and other non-conformal/non-cylindrical projections
(Albers Equal Area, Sinusoidal, Mollweide, Lambert Azimuthal Equal
Area, …) are rejected. Re-project the DEM (`r.proj`) into one of the
supported CRS first.

## PARAMETERS

| Option | Default | Description |
|--------|---------|-------------|
| `elevation` | — | Input DEM raster (metres). |
| `output` | — | Output basename; per-azimuth maps written as `basename_NNN_F` (mirrors *r.horizon*). |
| `direction` | — | Single azimuth, degrees CCW from east. Omit when using `start`/`end`/`step`. |
| `start` | 0 | Azimuth sweep start (degrees). |
| `end` | 360 | Azimuth sweep end exclusive (degrees). |
| `step` | — | Azimuth sweep step (degrees). |
| `maxdistance` | 10000 | Ray cap in metres. |
| `bodyradius` | 1737400 | Planetary radius (metres) for curvature correction. Default Moon. |
| `-c` | off | Force the OpenMP CPU backend even if OpenCL is available. |

## OUTPUT

One `FCELL` raster per azimuth, named `basename_NNN_F` where `NNN_F` is
the integer/decimal split of the azimuth (e.g. `22.5°` → `022_5`).
Values are in **degrees** (matching *r.horizon -d*).

## EXAMPLES

### Single azimuth (Moon)

```sh
p.horizon.gpu elevation=ldem_5m output=hor \
    direction=45 maxdistance=10000 bodyradius=1737400
```

### 16-azimuth sweep, force CPU backend (Mars)

```sh
p.horizon.gpu elevation=mola_30m output=mars_hor \
    start=0 end=360 step=22.5 \
    maxdistance=20000 bodyradius=3389500 -c
```

### Use as r.horizon replacement inside p_lib

```sh
# Any module that calls p_lib.precompute_horizons() will use the GPU
# backend automatically when this env var is set:
HORIZON_BACKEND=gpu grass ~/grassdata/Moon_SouthPole_5m/mapset --exec \
    p.visibility.earth elevation=ldem_5m mission=missions/artemis.json \
        horizon_step=22.5
```

## PERFORMANCE

Wall-clock dominates by the per-azimuth kernel launch, which scales
linearly in `nx × ny × (maxdistance/cell_m)`. On a Quadro P1000 and a
3000×3000 polar DEM at 5 m with `maxdistance=5 km` (1000 ray steps),
one azimuth completes in < 2 s; the equivalent *r.horizon*
single-threaded run is ≈ 3 min. The CPU/OpenMP fallback gives roughly
an order of magnitude less throughput than the GPU but still 3–6×
*r.horizon* with `nprocs=16`.

## NOTES

OpenCL device selection follows the `OPENCL_DEVICE` environment variable, or defaults to the first GPU device found. If no OpenCL device is available the module falls back to an OpenMP-parallel CPU path that is slower but otherwise identical. Output filenames follow the `r.horizon` convention (`basename_NNN_F` for azimuth NNN), so *p.horizon.gpu* is a drop-in replacement in any pipeline using that naming scheme.

## SEE ALSO

*[r.horizon](https://grass.osgeo.org/grass-stable/manuals/r.horizon.html), [p.sunmask](p.sunmask.md), [p.visibility.earth](p.visibility.earth.md), [p.visibility.orbiter](p.visibility.orbiter.md), [p.visibility.los](p.visibility.los.md)*

## AUTHORS

Yann Chemin, planetary remote sensing.
