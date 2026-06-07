## DESCRIPTION

**p.mcdm.score** performs two-phase multi-criteria suitability scoring.
Phase 1 applies hard binary exclusion masks. Phase 2 scores remaining pixels
by weighted linear combination (WLC) and/or TOPSIS.

### Phase 1 — Hard exclusion (binary masking)

Masked pixels (value = 1) receive suitability = 0 regardless of subsequent
scoring and are excluded from all further analysis.

### Phase 2a — Weighted Linear Combination (WLC)

Each criterion raster is normalised to [0, 1] (hazard rasters are inverted
so that 0 = safe maps to 1 after normalisation). The WLC score is:

```
S_WLC = Σ wᵢ × norm(cᵢ)      where Σ wᵢ = 1
```

This follows the weighted overlay approach of Golombek et al. (2003) and
Turchinskaya & Slyuta (2024).

### Phase 2b — TOPSIS (Hwang & Yoon 1981)

TOPSIS closeness coefficient Cᵢ:

```
Cᵢ = dᵢ⁻ / (dᵢ⁺ + dᵢ⁻)
```

where dᵢ⁺ is the weighted Euclidean distance from the positive ideal
solution and dᵢ⁻ is the distance from the negative ideal solution.
Cᵢ ∈ [0, 1]; higher = better.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `slope` | — | Normalised slope hazard raster [0=safe, 1=hazardous] |
| `roughness` | — | Normalised roughness hazard raster |
| `illumination` | — | Illumination fraction raster [0=dark, 1=always lit] |
| `earth_vis` | — | Earth visibility fraction raster |
| `science` | — | Science suitability raster [0=low, 1=high] |
| `exclusion_masks` | — | Comma-separated hard exclusion mask rasters (1=excluded) |
| `weights` | `0.25,0.15,0.20,0.15,0.25` | Criterion weights: slope, roughness, illumination, earth_vis, science |
| `method` | `both` | Scoring method: `wlc`, `topsis`, or `both` |
| `prefix` | `suitability` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_wlc` | WLC suitability score [0–1] |
| `<prefix>_topsis` | TOPSIS closeness coefficient [0–1] |
| `<prefix>_combined` | Mean of WLC and TOPSIS (if method=both) |

## EXAMPLES

### Standard Luna-27 MCDM scoring (WLC + TOPSIS)

```bash
# Assuming prior stages have generated these rasters:
# - slope_30m, roughness_rms, illum_fraction, earth_vis_fraction
# - slope_composite_mask, illum_psr_mask (exclusion masks)

p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             exclusion_masks=slope_composite_mask,illum_psr_mask \
             weights=0.25,0.15,0.20,0.15,0.25 \
             method=both prefix=suitability

# View results
r.univar map=suitability_wlc
r.univar map=suitability_topsis
r.univar map=suitability_combined
```

### Weighted Linear Combination (WLC) only (fast method)

```bash
# WLC is faster and more interpretable than TOPSIS
p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             exclusion_masks=slope_composite_mask \
             weights=0.25,0.15,0.20,0.15,0.25 \
             method=wlc prefix=suitability_wlc

# Check distribution
r.univar map=suitability_wlc
r.stats suitability_wlc columns=3 | tail -10
```

### TOPSIS only (prioritises trade-offs)

```bash
# TOPSIS identifies balanced scores (closer to ideal solution)
p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             exclusion_masks=slope_composite_mask \
             weights=0.25,0.15,0.20,0.15,0.25 \
             method=topsis prefix=suitability_topsis

# Compare with WLC
r.univar map=suitability_wlc
r.univar map=suitability_topsis
```

### Science-focused weighting (55% science priority)

```bash
# Favour science targets over engineering constraints
p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             science=volatile_ice_likelihood \
             weights=0.10,0.10,0.10,0.15,0.55 \
             method=both prefix=suitability_science

# Compare with default
r.univar map=suitability_wlc
r.univar map=suitability_science_wlc
```

### Multi-mission comparison with different weights

```bash
# Luna-27: balanced criteria
p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             weights=0.25,0.15,0.20,0.15,0.25 \
             method=both prefix=suitability_luna27

# Artemis: higher illumination priority
p.mcdm.score slope=slope_30m roughness=roughness_rms \
             illumination=illum_fraction earth_vis=earth_vis_fraction \
             weights=0.20,0.10,0.35,0.15,0.20 \
             method=both prefix=suitability_artemis

# Visualize comparison
d.mon start=wx
d.rast map=suitability_luna27_combined
d.rast map=suitability_artemis_combined
```

### Inspect hard exclusions and score bands

```bash
# Count pixels by score band
r.mapcalc "suitability_band = \
  if(suitability_combined >= 0.8, 5, \
  if(suitability_combined >= 0.6, 4, \
  if(suitability_combined >= 0.4, 3, \
  if(suitability_combined >= 0.2, 2, 1))))"

r.stats -c suitability_band
# Band 5 (0.8–1.0): excellent candidates
# Band 4 (0.6–0.8): good candidates
# Band 3 (0.4–0.6): fair candidates
# Band 2 (0.2–0.4): marginal candidates
# Band 1 (0.0–0.2): poor candidates
```

## NOTES

TOPSIS requires global ideal-best and ideal-worst distances, which are memory-intensive for large rasters; use `method=wlc` for quick pre-screening and switch to `method=topsis` for final analysis. Hard-exclusion masks (Phase 1) are applied before normalisation: masked pixels receive suitability = 0 and do not affect the global min/max used for Phase 2 normalisation.

## SEE ALSO

*[p.mcdm.weight](p.mcdm.weight.md),
[p.rank](p.rank.md),
[r.mcda.topsis](https://grass.osgeo.org/grass-stable/manuals/addons/r.mcda.topsis.html)*

## REFERENCES

- Golombek, M.P. et al. (2003) Selection of the Mars Exploration Rover
  landing sites. *Journal of Geophysical Research: Planets* 108(E12), 8072.
  doi:10.1029/2003JE002074
- Hwang, C.L. & Yoon, K. (1981) *Multiple Attribute Decision Making: Methods
  and Applications*. Springer. [TOPSIS method]
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
