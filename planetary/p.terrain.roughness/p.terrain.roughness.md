## DESCRIPTION

**p.terrain.roughness** computes three complementary roughness metrics in a
sliding window: RMS plane-detrended roughness, coefficient of variation of
slope (CV), and Moran's I spatial autocorrelation. All three are used in
*p.terrain.ellipse* to evaluate landing-zone suitability.

### RMS roughness

Root-mean-square of plane-detrended elevation residuals within the window.
Computed via *r.neighbors*: residual = elevation − neighbourhood mean,
RMS = sqrt(mean(residuals²)).

### Coefficient of Variation (Liu et al. 2023, Eq. 5)

CV quantifies dispersion of slope values within the window:

```
Cv = σ / μ
```

where σ is the standard deviation and μ the mean slope in the window.
A lower Cv indicates more spatially uniform terrain. Liu et al. (2023)
note that binarising slope at **8°** before computing Cv gives a more
uniformly distributed index than using 20° as a threshold.

### Moran's I (Liu et al. 2023, Eq. 6–8)

Moran's I measures spatial clustering of low-slope pixels:

```
         n    ΣᵢΣⱼ wᵢⱼ zᵢ zⱼ
I = ——— × ———————————————
        S₀       Σᵢ zᵢ²

S₀ = ΣᵢΣⱼ wᵢⱼ

wᵢⱼ = 1  if pixels i and j share an edge or vertex (Queen proximity)
wᵢⱼ = 0  otherwise
```

where zᵢ = xᵢ − X̄ (deviation from window mean slope). I ∈ [−1, +1];
values near +1 indicate strong clustering of safe (flat) terrain.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `slope` | — | Pre-computed slope raster; computed from dem if omitted |
| `window` | 11 | Analysis window size in pixels (must be odd) |
| `threshold` | 0.5 | RMS roughness threshold in metres for exclusion mask |
| `prefix` | `roughness` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_rms` | RMS plane-detrended roughness (metres) |
| `<prefix>_cv` | Coefficient of variation of slope (dimensionless) |
| `<prefix>_morans_i` | Moran's I spatial autocorrelation (−1 to +1) |
| `<prefix>_mask` | Binary exclusion mask (1 = RMS above threshold) |

## EXAMPLES

```bash
p.terrain.roughness dem=lola_5m prefix=roughness window=21 threshold=0.3
```

## PERFORMANCE AND HARDWARE TUNING

| Parameter | Default | Effect |
|---|---|---|
| `nprocs` | `1` | OpenMP threads for the many `r.slope.aspect` / `r.neighbors` calls inside the module (RMS roughness, CV of slope, Moran's I — eight calls in total). |
| `memory` | `300` | Row-cache size (MB) for the same calls. |

Forwarded automatically when called via `p.landing`.

```bash
# Laptop
p.terrain.roughness dem=lola_30m window=11 prefix=roughness

# 16-core workstation
p.terrain.roughness dem=lola_30m window=11 prefix=roughness \
    nprocs=16 memory=6000
```

## NOTES

RMS roughness window size should be at least 3× the expected landing-leg span to capture topography at the relevant scale (e.g. a 7-pixel window at 0.5 m/px for a 2 m leg-span lander). Moran's I is computed using a first-order queen contiguity weight matrix; values near +1 indicate spatially clustered roughness (ridge/valley terrain).

## SEE ALSO

*[p.terrain.slope](p.terrain.slope.md),
[p.terrain.ellipse](p.terrain.ellipse.md),
[r.neighbors](https://grass.osgeo.org/grass-stable/manuals/r.neighbors.html),
[r.roughness.vector](https://grass.osgeo.org/grass-stable/manuals/addons/r.roughness.vector.html)*

## REFERENCES

- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window. *Remote
  Sensing* 15, 3184, **Eq. 5** (CV), **Eq. 6–8** (Moran's I).
  doi:10.3390/rs15123184
- Moran, P.A.P. (1950) Notes on continuous stochastic phenomena.
  *Biometrika* 37(1–2), 17–23. [original Moran's I definition]

## AUTHOR

Yann Chemin
