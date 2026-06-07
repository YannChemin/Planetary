## DESCRIPTION

**p.visibility.earth** computes the fraction of time each surface pixel has
a direct line-of-sight to Earth over a complete planetary nutation cycle,
using pre-computed terrain horizon angles.

### Methodology (Turchinskaya & Slyuta 2024)

Earth visibility is checked at each timestep by comparing Earth's elevation
above the local terrain horizon to `min_elevation` (default 3°). This
follows Turchinskaya & Slyuta (2024), who required Earth elevation > 3° for
uninterrupted Luna-27 communication throughout its active mission period.

1. Horizon elevation angles in 16+ directions are pre-computed once with
   *r.horizon* (step mode).
2. At each timestep the sub-Earth point is determined from the Moon's
   libration model (±8° in longitude, ±7° in latitude over the nutation
   cycle).
3. Earth elevation and azimuth at the region centre are computed
   analytically.
4. The terrain horizon angle at that azimuth is linearly interpolated from
   the pre-computed horizon maps.
5. A pixel is Earth-visible if Earth elevation > horizon elevation +
   `min_elevation`.
6. Visibility fraction = fraction of timesteps the condition is satisfied.

### Engineering criterion (Turchinskaya & Slyuta 2024)

Luna-27 requires Earth visibility > 50%. All five South Polar candidate
sites achieve 85–92% average visibility, with peak values of 100% over
most of the ellipse area (Table 4 of Turchinskaya & Slyuta 2024).

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `body` | *required* | Body descriptor JSON |
| `nsteps` | 36 | Number of time steps over the nutation cycle. 1000+ recommended to capture libration cycles correctly per Turchinskaya & Slyuta 2024 |
| `min_elevation` | 3.0 | Minimum Earth elevation above horizon (degrees) |
| `horizon_step` | 22.5 | Angular step for pre-computing horizon maps (degrees) |
| `scan_res` | 0 | Resolution in metres for `r.horizon`. 0 = native DEM resolution. Set to e.g. 30 to coarsen the DEM before horizon computation — speeds up the polar-stereographic horizon pass by 1–2 orders of magnitude on dense DEMs. Computational region restored on exit. |
| `prefix` | `earth_vis` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_fraction` | Earth visibility fraction [0–1] |
| `<prefix>_mask` | 1 where fraction meets the engineering criterion |

## EXAMPLES

### Quick test (default 36 steps)
```bash
p.visibility.earth dem=lola_5m body=bodies/moon.json \
                   nsteps=36 min_elevation=3.0 prefix=earth_vis
```

### Article-grade run (Turchinskaya & Slyuta 2024 methodology)
1000 timesteps captures lunar libration to ±1°; `scan_res=30` aligns the
horizon computation with the article's working resolution and finishes in
minutes instead of hours.
```bash
p.visibility.earth dem=lola_5m body=bodies/moon.json \
                   nsteps=1000 min_elevation=3.0 \
                   horizon_step=22.5 scan_res=30 prefix=earth_vis_art
```

## PERFORMANCE AND HARDWARE TUNING

| Parameter | Default | Effect |
|---|---|---|
| `nprocs` | `1` | OpenMP threads for the `r.horizon` precompute (16 azimuths). Set to your physical core count for the production run; this one-time precompute is the only opportunity to parallelise. |
| `memory` | `300` | Forwarded to `r.horizon`; current GRASS releases ignore the keyword but accept it. |

Called via `p.landing`, both keys are forwarded automatically — and a
mission JSON `"nprocs"` / `"memory"` overrides the CLI verbatim.

Hardware-tuned examples (called directly):

```bash
# Laptop (4 cores)
p.visibility.earth dem=lola_30m body=bodies/moon.json nsteps=200 \
    horizon_step=22.5 prefix=earth_vis

# 16-core workstation
p.visibility.earth dem=lola_30m body=bodies/moon.json nsteps=1000 \
    horizon_step=22.5 nprocs=16 memory=6000 prefix=earth_vis
```

Approximate cost of the one-time horizon precompute on the Luna-27 sector
(62 M cells × 16 azimuths at 30 m): single-thread ~3 h, `nprocs=16`
~10–15 min. The per-step accumulator is a scalar `r.mapcalc` and does not
benefit from `nprocs`.

### GPU horizon backend (`HORIZON_BACKEND`)

Setting the environment variable `HORIZON_BACKEND=gpu` swaps the internal
*r.horizon* precompute call for *[p.horizon.gpu](p.horizon.gpu.md)* (OpenCL
ray-marching). On a Quadro P1000, the 16-azimuth precompute on a
3000×3000@5 m polar DEM drops from ~50 min (*r.horizon* with `nprocs=16`)
to seconds.

The GPU backend requires a **conformal** CRS (UTM, polar stereographic,
Lambert Conformal Conic, Mercator and variants); non-conformal projections
produce a fatal error. Horizon numerics differ from *r.horizon* on polar
DEMs (*p.horizon.gpu* uses a geometrically correct local-tangent walk; see
*[p.horizon.gpu](p.horizon.gpu.md)* for details). The `nprocs`/`memory`
options have no effect on the GPU path.

```bash
HORIZON_BACKEND=gpu grass ~/grassdata/Moon_SouthPole_5m/mapset --exec \
    p.visibility.earth dem=lola_5m body=bodies/moon.json nsteps=200 \
        horizon_step=22.5 prefix=earth_vis
```

## NOTES

Horizon rasters must be pre-computed with *p.horizon.gpu* or *r.horizon* and named via `horizon_prefix=`. For surface sites more than 60° from the mean sub-Earth point, Earth visibility is near 100% and this module is not required. The 3° minimum-elevation default follows the Luna-27 communications link budget (Turchinskaya & Slyuta 2024).

## SEE ALSO

*[p.visibility.los](p.visibility.los.md),
[p.visibility.orbiter](p.visibility.orbiter.md),
[r.horizon](https://grass.osgeo.org/grass-stable/manuals/r.horizon.html)*

## REFERENCES

- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011
  *(Earth visibility > 3° above horizon criterion; Table 4: 85–92% results)*

## AUTHOR

Yann Chemin
