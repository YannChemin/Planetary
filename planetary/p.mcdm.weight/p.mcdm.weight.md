## DESCRIPTION

**p.mcdm.weight** derives criterion weights from a user-supplied AHP pairwise
comparison matrix (Saaty 1977), validates the consistency ratio, and writes
results to a JSON file for use by *p.mcdm.score* and *p.rank*.

### Analytic Hierarchy Process (Saaty 1977)

1. **Pairwise matrix** A: n×n where A[i,j] = importance of criterion *i*
   relative to *j* on the 1–9 Saaty scale.
2. **Normalise**: divide each element by its column sum; weight vector *w* is
   the row mean of the normalised matrix.
3. **Principal eigenvalue** λmax = mean of (Aw)ᵢ / wᵢ over all *i*.
4. **Consistency Index** (Liu et al. 2023, Eq. 9):

   ```
   CI = (λmax − n) / (n − 1)
   ```

5. **Consistency Ratio** (Liu et al. 2023, Eq. 10):

   ```
   CR = CI / RI(n)
   ```

   Random Index RI: 0, 0, 0.58, 0.90, 1.12, 1.24, 1.32, 1.41, 1.45, 1.49
   for n = 1…10 (Saaty 1977).

6. CR ≤ 0.10 is required; otherwise the matrix must be revised.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `pairwise` | *required* | CSV file with n×n pairwise comparison matrix |
| `criteria` | *required* | Comma-separated criterion names (matching matrix order) |
| `output` | `weights.json` | Output JSON with weights and consistency metrics |

## FLAGS

| Flag | Description |
|------|-------------|
| `-v` | Verbose — print eigenvector, λmax, CI, CR to console |

## OUTPUT

JSON file with keys: `criteria`, `weights`, `lambda_max`, `CI`, `CR`,
`consistent`.

## EXAMPLES

```bash
cat > pcm.csv << 'EOF'
1,   3,   5
1/3, 1,   3
1/5, 1/3, 1
EOF

p.mcdm.weight pairwise=pcm.csv \
              criteria=slope,illumination,earth_vis \
              output=weights.json -v
```

## NOTES

A consistency ratio (CR) > 0.10 indicates the pairwise comparisons are internally inconsistent. The module prints a warning but still writes the weights; review and revise the matrix before using the weights in *p.mcdm.score*. The AHP pairwise matrix is supplied as a CSV file with one n×n row per line, no header.

## SEE ALSO

*[p.mcdm.score](p.mcdm.score.md),
[p.terrain.ellipse](p.terrain.ellipse.md)*

## REFERENCES

- Saaty, T.L. (1977) A scaling method for priorities in hierarchical
  structures. *Journal of Mathematical Psychology* 15(3), 234–281.
  doi:10.1016/0022-2496(77)90033-5
- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window. *Remote
  Sensing* 15, 3184, **Eq. 9–10** (CI, CR) and **Table 2** (weight matrix).
  doi:10.3390/rs15123184

## AUTHOR

Yann Chemin
