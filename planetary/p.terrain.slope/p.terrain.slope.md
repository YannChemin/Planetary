## DESCRIPTION

**p.terrain.slope** computes terrain slope at multiple length scales relevant
to planetary landing and generates a binary hazard exclusion mask at each
scale, plus a composite worst-case mask.

At each scale the DEM is resampled with *r.resamp.stats* (average), slope is
computed with *r.slope.aspect*, and pixels exceeding the threshold are flagged
as excluded (mask = 1). The composite mask is 1 wherever any scale flags the
pixel as dangerous.

### Slope computation (Liu et al. 2023, Eq. 1–3)

Slope is computed from the standard finite-difference Horn (1981) formula:

```
dz/dx = [(c + 2f + i) − (a + 2d + g)] / (8 × L)
dz/dy = [(g + 2h + i) − (a + 2b + c)] / (8 × L)
S = arctan( sqrt( (dz/dx)² + (dz/dy)² ) )    [degrees]
```

where *a–i* are the elevations of the 3×3 neighbourhood (row-major, centre
= *e*) and *L* is the pixel size in metres.

### Scale rationale (Turchinskaya & Slyuta 2024)

Safe landing requires slopes < 7–10° on the landing-ellipse scale and
< 15° on the footpad scale. Recommended defaults:

| Scale | Threshold | Meaning |
|-------|-----------|---------|
| 30 m | 15° | Footpad hazard |
| 100 m | 10° | Lander body clearance |
| 1000 m | 7° | Descent guidance |
| 10 000 m | 5° | Approach corridor |

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `scales` | `30,100,1000,10000` | Analysis scales in metres |
| `thresholds` | `15,10,7,5` | Exclusion thresholds in degrees |
| `prefix` | `slope` | Output map name prefix |

## FLAGS

| Flag | Description |
|------|-------------|
| `-k` | Keep per-scale resampled DEM rasters |

## OUTPUT MAPS

| Map | Contents |
|-----|----------|
| `<prefix>_<scale>m` | Slope in degrees at each scale |
| `<prefix>_mask_<scale>m` | Binary exclusion mask (1=hazardous) |
| `<prefix>_composite_mask` | 1 where any scale exceeds its threshold |

## EXAMPLES

### Default multi-scale slope analysis (Luna-27 thresholds)

```bash
# Default: 30m/15°, 100m/10°, 1000m/7°, 10000m/5°
p.terrain.slope dem=lola_5m prefix=slope

# View generated maps
g.list type=raster pattern="slope*"
r.stats slope_30m columns=4 | head -20
```

### Single-scale slope at 30 m (footpad hazard)

```bash
# Footpad-scale hazard assessment with 15° threshold
p.terrain.slope dem=lola_5m scales=30 thresholds=15 prefix=slope_footpad

# Display the hazard mask (1=unsafe)
r.colors map=slope_footpad_mask_30m color=red
d.mon start=wx
d.rast map=slope_footpad_mask_30m
d.barscale

# Count hazardous pixels
r.univar map=slope_footpad_mask_30m
```

### Strict multi-scale analysis (heavier lander requirements)

```bash
# More stringent slope criteria
p.terrain.slope dem=lola_5m \
               scales=30,100,1000,10000 \
               thresholds=12,8,5,3 \
               prefix=slope_strict

# Compare with default
r.stats -c slope_composite_mask
r.stats -c slope_strict_composite_mask
```

### Inspect multi-scale slope variations

```bash
# Run multi-scale analysis and keep resampled DEMs
p.terrain.slope dem=lola_5m prefix=slope -k

# Summary statistics per scale
r.univar map=slope_30m
r.univar map=slope_100m
r.univar map=slope_1000m
r.univar map=slope_10000m

# Composite hazard: fraction of scales exceeding threshold
r.mapcalc "hazard_fraction = (slope_mask_30m + slope_mask_100m + slope_mask_1000m + slope_mask_10000m) / 4.0"
```

## PERFORMANCE AND HARDWARE TUNING

| Parameter | Default | Effect |
|---|---|---|
| `nprocs` | `1` | OpenMP threads for `r.slope.aspect` (one of the few core GRASS modules that scales with `nprocs`). |
| `memory` | `300` | Row-cache size (MB) for `r.slope.aspect`. Raise on machines with plenty of RAM to reduce row I/O on large DEMs. |

Called via `p.landing`, both are forwarded from the orchestrator (mission
JSON keys win if set).

```bash
# Laptop — defaults are sensible
p.terrain.slope dem=lola_30m scales=30,100,1000 thresholds=15,10,5

# 16-core workstation
p.terrain.slope dem=lola_30m scales=30,100,1000 thresholds=15,10,5 \
    nprocs=16 memory=6000
```

## NOTES

Scales are computed independently and the composite exclusion mask is a logical OR: a pixel excluded at any single scale is excluded in the composite. Slope scales should bracket the landing-ellipse major axis and the individual footpad spacing. Very fine scales (< 5× pixel size) may amplify DEM noise; apply *p.dem.prep* smoothing first.

## SEE ALSO

*[p.terrain.roughness](p.terrain.roughness.md),
[p.terrain.hazard](p.terrain.hazard.md),
[p.terrain.ellipse](p.terrain.ellipse.md),
[r.slope.aspect](https://grass.osgeo.org/grass-stable/manuals/r.slope.aspect.html)*

## REFERENCES

- Horn, B.K.P. (1981) Hill shading and the reflectance map. *Proc. IEEE*
  69(1), 14–47. doi:10.1109/PROC.1981.11918
- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window. *Remote
  Sensing* 15, 3184, **Eq. 1–3** (slope formula).
  doi:10.3390/rs15123184
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
