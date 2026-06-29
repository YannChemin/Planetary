## DESCRIPTION

*p.spec.pca* computes **Principal Component Analysis** (PCA) of a
multi-band planetary raster. It reads bands named `input.1`, `input.2`,
…, builds the per-pixel covariance (or correlation) matrix, performs an
exact eigendecomposition via the cyclic Jacobi algorithm, and writes the
requested number of PC score rasters in descending explained-variance
order.

### What PCA does in planetary spectroscopy

PCA rotates the band space so that PC-1 captures the most variance
(often dominated by albedo / illumination), PC-2 captures the next
independent source of variance (often a broad spectral slope), and
higher PCs progressively isolate weaker, instrument-noise-free signal
components (absorption features, compositional boundaries). Displaying
PC-2/PC-3/PC-4 as a false-colour composite is the standard first step
for highlighting mineralogic boundaries in CRISM, OMEGA, and M3 data.

### Covariance vs correlation PCA (`-s` flag)

Without `-s` (default) the covariance matrix of the raw band values is
used. This is appropriate when all bands are in the same physical unit
and comparable range (e.g. CRISM I/F reflectance). With `-s` each band
is first divided by its standard deviation (equivalent to using the
correlation matrix). Use `-s` when bands span very different value
ranges, such as when mixing VNIR and SWIR channels with different
absolute radiance scales.

### Eigenvalue CSV (`stats=`)

The output CSV contains one row per PC with columns:
- `PC`: PC number (1 = first, highest-variance)
- `eigenvalue`: variance of the data projected onto this PC
- `variance_pct`: percentage of total variance explained
- `cumulative_pct`: cumulative variance explained
- `evec_band1` … `evec_bandN`: eigenvector components (loadings)

The loadings show which input bands contribute most to each PC. A large
positive or negative loading at a specific wavelength indicates that
absorption feature drives that PC.

## NOTES

- All input pixels where **any** band is NULL are excluded from the
  covariance computation and set to NULL in all output PC rasters.
- The Jacobi algorithm converges to machine precision for any symmetric
  matrix; convergence is typically reached in ≤ 40 sweeps.
- For large cubes (> 400 bands, e.g. CRISM 544 bands × 300 k pixels),
  the covariance accumulation pass takes on the order of 10–30 seconds.
  The eigendecomposition itself is fast (< 5 s for 544 × 544).
- Setting `ncomps=` to a small number (e.g. 10) is recommended for
  visualisation; all bands are still used in the covariance computation.

## EXAMPLES

Basic PCA of a CRISM VNIR group (544 bands, default covariance):

```sh
p.spec.pca input=crism_frt0003bfb output=crism_pc \
    ncomps=10 stats=crism_pca_stats.csv
```

Display PC-2/PC-3/PC-4 as false colour (standard mineralogy composite):

```sh
d.rgb red=crism_pc.2 green=crism_pc.3 blue=crism_pc.4
```

Correlation-matrix PCA (standardised bands) for OMEGA with mixed channels:

```sh
p.spec.pca input=omega_orb100 output=omega_pc -s ncomps=6
```

## REFERENCES

- Boardman, J.W. & Kruse, F.A. (1994). Automated spectral analysis: a
  geologic example using AVIRIS data, north Grapevine Mountains, Nevada.
  *Proc. ERIM Conf.*, 407–418.

- Pelkey, S.M. et al. (2007). CRISM multispectral summary products.
  *J. Geophys. Res.* 112:E08S14. doi:10.1029/2006JE002831.

- Golub, G.H. & Van Loan, C.F. (2013). *Matrix Computations*, 4th ed.
  Johns Hopkins Press. (Jacobi algorithm: §8.4.)

## SEE ALSO

*p.spectral.planet*, *p.mineral.indices*, *p.spec.class*
