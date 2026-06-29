## DESCRIPTION

*p.spec.class* classifies multi-band planetary rasters spectrally in two modes:

### kmeans — unsupervised spectral clustering

Groups pixels into `k` spectrally homogeneous classes using the
k-means++ algorithm (Arthur & Vassilvitskii 2007). k-means++ uses
distance-weighted probabilistic seeding to avoid the degenerate cluster
initialisation that naive random seeding often produces.

Output is an integer raster where each pixel value is its class label
(1 … k). Pixels where any band is NULL are set to NULL in the output.
Optionally writes the centroid spectrum for each class to a CSV file
(`stats=`), enabling post-hoc spectral identification of the classes.

**Typical workflow for geologic unit mapping:**

```
1. Import a calibrated CRISM/OMEGA/M3 cube via p.in.archive
2. Optionally run p.spec.pca to denoise (use first N PCs as input)
3. Run p.spec.class mode=kmeans k=8 to produce a unit raster
4. Inspect class centroids in stats CSV; label each class geologically
5. Use v.what.rast or r.category to attach geologic names to classes
```

### sam — supervised Spectral Angle Mapper classification

Compares each pixel's spectrum to a single reference spectrum supplied
as a CSV file (one reflectance value per line, one band per line).
Pixels whose SAM angle ≤ `threshold` radians are assigned value 1
(match); all others are 0. NULL pixels remain NULL.

The default threshold of 0.1 rad (≈ 5.7°) is a commonly used starting
point for mineralogic matching; tighten to 0.05 rad for higher purity,
relax to 0.15–0.2 rad for higher completeness.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `input=` | required | Base name of input bands (`input.1`, `.2`, …) |
| `output=` | required | Output raster (integer class labels) |
| `mode=` | required | `kmeans` or `sam` |
| `k=` | 5 | [kmeans] Number of spectral classes (2–255) |
| `iterations=` | 100 | [kmeans] Maximum k-means iterations |
| `seed=` | 0 | [kmeans] Random seed (0 = system time) |
| `spectrum=` | | [sam] Reference spectrum CSV file |
| `threshold=` | 0.1 | [sam] SAM angle threshold (radians) |
| `stats=` | | Output CSV: centroids (kmeans) or match stats (sam) |

## NOTES

- **Memory**: k-means loads all valid pixels into memory. For a CRISM
  scene (480 × 640 = 307,200 pixels × 544 bands × 8 bytes ≈ 1.3 GB).
  Use `p.spec.pca` first to reduce to 10–20 PCs before k-means if
  memory or convergence is an issue.
- **k selection**: The "elbow method" on within-cluster variance is
  the standard approach. Run with k=4,6,8,10,12 and compare the
  centroid CSV files; the k where adding one more class stops
  significantly changing the centroids is a reasonable choice.
- **Determinism**: Set `seed=` to a fixed value for reproducible results.

## EXAMPLES

8-class unsupervised classification of CRISM PCs 1–10:

```sh
p.spec.class input=crism_pc output=crism_units mode=kmeans k=8 \
    stats=crism_centroids.csv seed=42
```

SAM match against a laboratory olivine spectrum at 5.7° threshold:

```sh
p.spec.class input=crism_frt output=crism_olivine_match \
    mode=sam spectrum=olivine_lab.csv threshold=0.1
```

## REFERENCES

- Arthur, D. & Vassilvitskii, S. (2007). k-means++: The advantages of
  careful seeding. *Proc. SODA*, 1027–1035.

- Kruse, F.A. et al. (1993). The Spectral Image Processing System (SIPS).
  *Remote Sensing of Environment* 44:145–163.

- Pelkey, S.M. et al. (2007). CRISM multispectral summary products.
  *J. Geophys. Res.* 112:E08S14.

## SEE ALSO

*p.spec.pca*, *p.spectral.planet*, *p.mineral.indices*
