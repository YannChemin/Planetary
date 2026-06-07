## DESCRIPTION

**p.terrain.hazard** combines slope, roughness, local relief, crater density,
and profile curvature into a single normalised composite hazard score [0, 1]
and a hard binary exclusion mask. It is the summary terrain layer that feeds
into *p.mcdm.score*.

### Composite hazard formula

Each criterion is normalised to [0, 1] (0 = safe, 1 = hazardous) and
combined with a weighted linear combination:

```
H = w_slope  × norm(slope)
  + w_rough  × norm(roughness_rms)
  + w_relief × norm(local_relief)
  + w_crater × norm(crater_density)
  + w_curv   × norm(|profile_curvature|)
```

where Σwᵢ = 1. Default weights (0.40, 0.25, 0.15, 0.10, 0.10) reflect the
relative importance of slope and roughness (Golombek et al. 2003;
Turchinskaya & Slyuta 2024).

### Hard exclusion mask

Pixels exceeding the slope threshold (default 15°) or roughness threshold
(default 1.0 m RMS) are hard-excluded (mask = 1) regardless of score.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `slope` | — | Pre-computed slope raster; computed if omitted |
| `roughness` | — | Pre-computed RMS roughness raster; computed if omitted |
| `craters` | — | Crater density raster; included if provided |
| `weights` | `0.40,0.25,0.15,0.10,0.10` | Criterion weights: slope, roughness, relief, craters, curvature |
| `slope_max` | 15.0 | Hard slope exclusion threshold (degrees) |
| `roughness_max` | 1.0 | Hard roughness exclusion threshold (metres RMS) |
| `prefix` | `hazard` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_score` | Composite hazard score [0–1] |
| `<prefix>_mask` | Hard binary exclusion mask (1=excluded) |

## EXAMPLES

```bash
p.terrain.hazard dem=lola_5m slope=slope_30m roughness=roughness_rms \
                 prefix=hazard
```

## PERFORMANCE AND HARDWARE TUNING

| Parameter | Default | Effect |
|---|---|---|
| `nprocs` | `1` | OpenMP threads for the internal `r.slope.aspect` / `r.neighbors` calls. |
| `memory` | `300` | Row-cache size (MB) for the same calls. |

Forwarded automatically when called via `p.landing`.

```bash
# Laptop
p.terrain.hazard dem=lola_30m prefix=hazard

# 16-core workstation
p.terrain.hazard dem=lola_30m prefix=hazard nprocs=16 memory=6000
```

## NOTES

Default weights (slope 0.40, roughness 0.30, relief 0.10, crater density 0.10, curvature 0.10) reflect a conservative safety-first strategy. For science-driven landing (e.g. boulder-rich outcrops) consider reducing the slope weight and increasing the crater-density weight. The binary exclusion mask uses a hard threshold (default 0.60) on the composite hazard score.

## SEE ALSO

*[p.terrain.slope](p.terrain.slope.md),
[p.terrain.roughness](p.terrain.roughness.md),
[p.mcdm.score](p.mcdm.score.md),
[r.param.scale](https://grass.osgeo.org/grass-stable/manuals/addons/r.param.scale.html)*

## REFERENCES

- Golombek, M.P. et al. (2003) Selection of the Mars Exploration Rover
  landing sites. *Journal of Geophysical Research: Planets* 108(E12), 8072.
  doi:10.1029/2003JE002074
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
