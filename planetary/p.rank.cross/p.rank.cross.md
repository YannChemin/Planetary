## DESCRIPTION

**p.rank.cross** ingests multiple `p.rank` JSON reports — one per region or
per mission variant — and produces a single ranked table of the best
candidates across all of them. It is the natural top-of-pipeline step when
the same toolkit is run independently on N candidate regions (e.g. the
NASA Artemis III shortlist of ~13 polar regions) and a unified ranking is
needed for mission planning.

The composite score per candidate is a weighted combination of three
normalised components:

```
composite = w_suit  · norm(suit_mean)
          + w_area  · norm(transform(area_km2))
          + w_borda · norm(borda_score)
```

where `borda_score` is an intra-region rank-based score (top candidate in
a region with K candidates gets K points, …, last gets 1), optionally
multiplied by the candidate's Monte-Carlo `rank1_probability` (from
`p.rank`) so that weight-uncertainty information from the per-region
analyses survives the cross-rank step.

The area transform defaults to `log(1+area)` which compresses the
typical orders-of-magnitude spread between small inter-crater patches
and ellipse-sized safe regions; `linear` and `sqrt` are also supported.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `reports` | *required* | Comma-separated list of `p.rank` JSON report files |
| `region_ids` | *report stems* | Override region labels (must match reports order) |
| `weights` | `suit:0.6,area:0.25,borda:0.15` | Composite-score component weights; renormalised to sum=1 |
| `area_transform` | `log` | `linear`, `sqrt` or `log` (`log1p`) |
| `n_top` | `20` | Maximum candidates retained in the cross-ranked output |
| `output` | `cross_ranking_report.json` | Output JSON path |

## OUTPUT FORMAT

The output JSON has the schema tag `p.rank.cross/v1` and contains:

- `weights`, `area_transform`, `n_regions`, `n_candidates`, `n_returned`
- `regions[]`: per-region metadata (id, source report path, candidate count)
- `candidates[]`: top-`n_top` entries, each carrying `cross_rank`,
  `region`, `intra_region_rank`, `composite_score`, the original
  `suit_mean`, `suit_std`, `area_km2`, `rank1_probability`, and the
  three normalised component scores under `components`.

## EXAMPLES

### Default cross-rank of three regions

```bash
p.rank.cross reports=region_a_report.json,region_b_report.json,region_c_report.json \
             output=cross_ranking.json
```

### Up-weight area when small patches are not operationally viable

```bash
p.rank.cross reports=r1.json,r2.json,r3.json \
             weights=suit:0.4,area:0.5,borda:0.1 \
             area_transform=linear  output=area_first.json
```

### NASA Artemis-style multi-region shortlist

```bash
# Run p.landing once per region with separate mapsets, then aggregate
p.rank.cross \
  reports=$(ls ~/grassdata/Moon_SouthPole_5m/artemis_*_report.json | paste -sd,) \
  region_ids=connecting_ridge,de_gerlache,malapert_massif,nobile_rim,mons_mouton \
  n_top=30 output=artemis_shortlist.json
```

## NOTES

All input JSON files must have been produced by the same version of *p.rank* because the schema is validated for required keys. Per-region normalisation is applied before cross-region aggregation, so absolute suitability values need not be comparable across regions. The output JSON can be passed directly to *g.gui.landing* for visualisation.

## SEE ALSO

*[p.rank](p.rank.md),
[p.landing](p.landing.md),
[p.mcdm.score](p.mcdm.score.md)*

## AUTHOR

Yann Chemin
