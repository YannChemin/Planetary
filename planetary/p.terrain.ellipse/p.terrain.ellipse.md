## DESCRIPTION

**p.terrain.ellipse** scans the DEM with a sliding window of landing-ellipse
dimensions and computes the AHP-weighted overall rating Q (Liu et al. 2023,
Eq. 11) for each window position. High-Q windows are exported as ranked
vector polygons.

### Metrics computed per window

**1. Mean slope** — average slope in degrees. A lower mean satisfies the basic
flat-terrain requirement for safe touchdown (Turchinskaya & Slyuta 2024).

**2. Threshold ratio** (Liu et al. 2023, Eq. 4) — fraction of pixels below a
slope threshold:

```
S = p(<threshold) / P
```

where p(<threshold) = count of pixels below the threshold, P = total pixels.
Computed at two thresholds: low (default 8°) and high (default 20°).

**3. Coefficient of Variation** (Liu et al. 2023, Eq. 5):

```
Cv = σ / μ
```

**4. Moran's I** (Liu et al. 2023, Eq. 6–8):

```
         n    ΣᵢΣⱼ wᵢⱼ zᵢ zⱼ
I = ——— × ———————————————      wᵢⱼ = 1 (Queen adjacency), else 0
        S₀       Σᵢ zᵢ²
```

### Overall rating Q (Liu et al. 2023, Eq. 11)

AHP-weighted combination of the five metrics (weights from Liu et al. 2023
Table 2, CR = 2.5% ≤ 10%):

```
Q = 0.3632 × Mean
  + 0.3632 × Ratio(8°)
  + 0.07667 × Ratio(20°)
  + 0.15782 × (1 − Cv)
  + 0.03912 × Moran'sI
```

Mean is normalised to 1 if mean slope < 8°, else 0. Q ∈ [0, 1].

### AHP consistency check (Liu et al. 2023, Eq. 9–10)

```
CI = (λmax − n) / (n − 1)
CR = CI / RI(n)
```

For the 5-criterion matrix: λmax = 5.1127, CI = 0.02818, RI(5) = 1.12,
CR = 2.5% (accepted, < 10%).

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM (metres) |
| `slope` | — | Pre-computed slope raster; computed if omitted |
| `ellipse_major` | 30000 | Landing ellipse major axis (metres) |
| `ellipse_minor` | 15000 | Landing ellipse minor axis (metres) |
| `scan_res` | 1000 | Working resolution for window scan (metres) |
| `slope_threshold_low` | 8.0 | Low slope threshold for Ratio (degrees) |
| `slope_threshold_high` | 20.0 | High slope threshold for Ratio (degrees) |
| `weights` | `0.3632,0.3632,0.0767,0.1578,0.0391` | AHP weights: mean, ratio_low, ratio_high, cv, moransI |
| `prefix` | `ellipse` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_Q` | Overall rating Q raster [0–1] |
| `<prefix>_candidates` | Vector polygons of candidate windows, attributed with Q and all sub-metrics |

## EXAMPLES

```bash
# Standard Luna-27 scan (30 × 15 km ellipse)
p.terrain.ellipse dem=lola_5m ellipse_major=30000 ellipse_minor=15000 \
                  scan_res=500 prefix=luna27_ellipse
```

## NOTES

The default window stride is `min(major_axis, minor_axis) / 4` pixels; reducing it improves coverage at the cost of quadratically increasing computation time. The Q rating (Liu et al. 2023, Eq. 11) integrates slope mean, threshold ratio, roughness, and crater density with AHP-derived weights; supply a weight JSON from *p.mcdm.weight* via `weights=` to override the defaults.

## SEE ALSO

*[p.terrain.slope](p.terrain.slope.md),
[p.terrain.roughness](p.terrain.roughness.md),
[p.mcdm.weight](p.mcdm.weight.md)*

## REFERENCES

- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window. *Remote
  Sensing* 15, 3184. doi:10.3390/rs15123184
  **Eq. 1–3** (slope), **Eq. 4** (threshold ratio), **Eq. 5** (CV),
  **Eq. 6–8** (Moran's I), **Eq. 9–10** (AHP CI/CR), **Eq. 11** (Q).
- Saaty, T.L. (1977) A scaling method for priorities in hierarchical
  structures. *Journal of Mathematical Psychology* 15(3), 234–281.
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
