## DESCRIPTION

**p.rank** extracts top candidate landing sites from a suitability map, ranks
them by composite score, runs Monte Carlo weight sensitivity analysis, and
produces a ranked vector map and a JSON report.

### Candidate extraction

1. Suitability is thresholded at the `top_percentile` (default 85th) computed
   over non-zero pixels only.
2. Pixels above the threshold are binarised to 1; others become NULL.
3. *r.clump* labels connected regions. Regions smaller than `min_area_km2`
   (default 50 km²) are discarded.
4. Zonal statistics (mean suitability, area, per-criterion means) are computed
   with *r.univar*.
5. Regions are ranked 1 (best) to N by mean suitability.

### Monte Carlo sensitivity analysis

For each of `mc_samples` iterations:

1. Criterion weights are randomly perturbed with Dirichlet noise, re-normalised
   to sum to 1.
2. Each candidate's composite score is recomputed as the weighted mean of its
   per-criterion zonal averages.
3. Candidates are re-ranked.

The report records the fraction of MC runs in which each candidate ranks first
(robustness) and the standard deviation of its rank (sensitivity).

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `suitability` | *required* | Final suitability raster [0–1] |
| `criteria` | — | Comma-separated criterion rasters for per-site statistics |
| `min_area_km2` | 50.0 | Minimum candidate region area in km² |
| `top_percentile` | 85.0 | Suitability percentile threshold |
| `n_candidates` | 10 | Maximum number of candidates to report |
| `mc_samples` | 200 | Monte Carlo weight perturbation samples |
| `prefix` | `rank` | Output map name prefix |
| `report` | `landing_ranking_report.json` | Output JSON report filename |

## OUTPUT

| Output | Description |
|--------|-------------|
| `<prefix>_candidates` | Raster map of candidates labelled by rank |
| `<prefix>_ranked_vector` | Vector polygons with rank, area, mean suitability, per-criterion means, MC robustness |
| report JSON | Machine-readable ranking and sensitivity results |

### Graceful failure when no candidates meet the threshold

When the area filter rejects every clump (a common situation with very
strict `top_percentile` or large `min_area_km2`), p.rank **does not abort**.
It emits a warning, exits 0, and writes a diagnostic JSON report
containing the largest contiguous region found, e.g.:

```json
{
  "n_candidates_found": 0,
  "status": "no_candidates_above_threshold",
  "threshold_percentile": 85.0,
  "threshold_value": 0.5874,
  "min_area_km2": 353.43,
  "largest_candidate": {
    "clump_cat": 42,
    "area_km2": 12.7,
    "n_clumps": 318
  },
  "candidates": []
}
```

This lets a pipeline (e.g. p.landing) record the failure cleanly and
suggests how to tune parameters. If the suitability map has no clumps
at all above the threshold, `largest_candidate.area_km2` is `0`.

## EXAMPLES

### Standard Luna-27 candidate extraction and ranking

```bash
# Extract top 5 candidates using 85th percentile, 500 MC iterations
p.rank suitability=suitability_combined \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       min_area_km2=50 top_percentile=85 n_candidates=5 \
       mc_samples=500 prefix=rank report=luna27_ranking.json

# View results
jq '.candidates[0]' luna27_ranking.json
r.univar map=rank_candidates
```

### High-precision sensitivity analysis (1000 MC samples)

```bash
# More MC iterations for robust estimates
p.rank suitability=suitability_combined \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       min_area_km2=50 top_percentile=85 n_candidates=5 \
       mc_samples=1000 prefix=rank_robust report=landing_robust.json

# Check ranking stability
jq '.candidates[] | {rank, robustness}' landing_robust.json
```

### Relaxed candidate extraction (90th percentile, larger pool)

```bash
# Extract more candidates with looser threshold
p.rank suitability=suitability_combined \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       min_area_km2=20 top_percentile=90 n_candidates=10 \
       mc_samples=500 prefix=rank_relaxed report=candidates_relaxed.json
```

### Strict candidate extraction (top 3 finalists only)

```bash
# Extract only very best candidates
p.rank suitability=suitability_combined \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       min_area_km2=100 top_percentile=75 n_candidates=3 \
       mc_samples=500 prefix=rank_strict report=top3_candidates.json

# View top finalists
jq '.candidates[] | select(.robustness > 0.8)' top3_candidates.json
```

### Visualize candidates and their robustness

```bash
# Display candidates coloured by rank
d.mon start=wx
d.rast map=suitability_combined
d.vect map=rank_ranked_vector attribute=rank_position

# Export for GIS analysis
v.out.ogr input=rank_ranked_vector output=candidates.gpkg format=GPKG
```

### Interpret Monte Carlo sensitivity report

```bash
# View overall report structure
jq '.' luna27_ranking.json | head -30

# Extract robustness: fraction of MC runs where candidate ranked 1st
# robustness > 0.8 = stable, insensitive to weight changes
# robustness < 0.3 = sensitive, ranking depends on weight assumptions

jq '.candidates[] | {rank, robustness, mean_suitability}' luna27_ranking.json

# Top candidate's component scores
jq '.candidates[0] | {terrain_score, illumination_score, visibility_score, science_score}' luna27_ranking.json
```

### Multi-mission candidate comparison

```bash
# Luna-27 ranking
p.rank suitability=suitability_luna27 \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       n_candidates=5 mc_samples=500 prefix=rank_luna27 report=luna27_ranking.json

# Artemis ranking
p.rank suitability=suitability_artemis \
       criteria=slope_30m,roughness_rms,illum_fraction,earth_vis_fraction \
       n_candidates=5 mc_samples=500 prefix=rank_artemis report=artemis_ranking.json

# Compare top 3 candidates
jq '.candidates[0:3]' luna27_ranking.json
jq '.candidates[0:3]' artemis_ranking.json
```

## NOTES

The Monte Carlo weight-perturbation analysis draws weights from a Dirichlet distribution centred on the input weights with concentration parameter controlled by `mc_sigma`. The reported rank-1 probability is the fraction of Monte Carlo trials in which a candidate remains the top-ranked site — a direct measure of ranking robustness to weight uncertainty.

## SEE ALSO

*[p.mcdm.score](p.mcdm.score.md),
[r.clump](https://grass.osgeo.org/grass-stable/manuals/r.clump.html),
[r.univar](https://grass.osgeo.org/grass-stable/manuals/r.univar.html)*

## REFERENCES

- Golombek, M.P. et al. (2003) Selection of the Mars Exploration Rover landing
  sites. *Journal of Geophysical Research: Planets* 108(E12), 8072.
  doi:10.1029/2003JE002074 *(multi-criteria ranking methodology)*
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011 *(priority 1–5 ranking of five sites)*

## AUTHOR

Yann Chemin
