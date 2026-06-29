## DESCRIPTION

*p.spec.library* compares a query spectrum against a directory of 2-column
wavelength/reflectance CSVs and reports the top-N closest matches ranked by
**Spectral Angle Mapper (SAM)** distance.

The query spectrum can be:

- a CSV file (`spectrum=`) — two columns: `wavelength_um,reflectance`; lines
  starting with `#` are treated as comments and ignored.
- the mean spectrum of a GRASS imagery group over the current region
  (`group=` + optional `wavelengths=`); the mean of each band raster is
  extracted via `r.univar`.

The library is any directory of 2-column CSV files (`library=`). The
**built-in planetary library** (28 spectra from USGS splib07a; no external
download required) is used by default.

### Built-in library contents

| Class | Spectra |
|---|---|
| Olivine | Fo89, Fo51, Fo29, Fo11 (compositional series) |
| Low-Ca pyroxene | Enstatite, Bronzite |
| High-Ca pyroxene | Augite, Diopside |
| Plagioclase | Anorthite, Albite |
| Smectite / phyllosilicate | Nontronite, Montmorillonite, Saponite, Kaolinite, Serpentine, Chlorite, Illite, Muscovite, Talc |
| Secondary silicate | Prehnite, Epidote |
| Carbonate | Calcite |
| Sulfate | Gypsum, Jarosite (Na), Jarosite (K) |
| Iron oxide | Hematite, Goethite |
| Silica | Opal |

All BECK-instrument spectra cover 0.2–3.0 µm (≈ 460–478 valid channels);
ASD-instrument spectra (serpentine, chlorite, goethite) cover 0.35–2.5 µm
(2151 channels). Spectra are resampled to the query's wavelength grid via
linear interpolation before SAM computation.

### SAM angle

SAM is the arccosine of the normalised dot product of two spectral vectors:

```
SAM = acos(Σ aᵢbᵢ / (|a|·|b|))
```

It is invariant to multiplicative (albedo) scaling and gives distances in
radians (0 = identical shape). Typical thresholds for mineralogic matching:

- 0.05 rad (≈ 2.9°) — high purity
- 0.10 rad (≈ 5.7°) — standard starting point
- 0.20 rad (≈ 11.5°) — high completeness / mixed surfaces

### Using an external RELAB or USGS splib07a library

Point `library=` at any directory containing 2-column CSV files. You can
convert USGS splib07a ASCII files with the included `make_speclib.py` script
(edit the `SPLIB07` path variable at the top). RELAB files (3-column:
wavelength, reflectance, stddev) need minor pre-processing — strip the first
two header lines and drop the third column.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `spectrum=` | | Query spectrum CSV (wavelength_um, reflectance) |
| `group=` | | GRASS imagery group to extract mean spectrum from |
| `wavelengths=` | | Per-band wavelength CSV for `group=` mode (one value per line) |
| `library=` | built-in | Directory of 2-column spectrum CSVs |
| `top=` | 10 | Number of top matches to report |
| `output=` | | CSV file for the ranked match table (default: stdout) |
| `max_angle=` | π/2 | Maximum SAM angle in radians; larger matches are excluded |
| `-v` | | Verbose: print all library entries considered |

## NOTES

- Library entries are sorted alphabetically before matching; the rank order
  among equal-angle entries is alphabetical.
- Wavelengths outside the library spectrum's range are treated as NaN and
  excluded from the dot product. This means a query with wavelengths extending
  beyond 2.5 µm will automatically use fewer bands when comparing against
  ASD-instrument library entries.
- For group= mode without `wavelengths=`, band indices (1, 2, 3…) are used as
  "wavelengths"; this makes SAM angles meaningless for cross-library comparison
  but still valid for intra-group unsupervised ordering. Always supply `wavelengths=`
  for meaningful results.

## EXAMPLES

Match a mean CRISM spectrum (extracted from the current region of an imported
CRISM imagery group) against the built-in library:

```sh
p.spec.library group=crism_frt wavelengths=crism_wavelengths.csv top=5
```

Match a single-pixel spectrum CSV against the built-in library, save results:

```sh
p.spec.library spectrum=my_pixel.csv top=10 output=matches.csv
```

Match against a full USGS splib07a directory (requires local copy):

```sh
p.spec.library spectrum=my_pixel.csv \
    library=/path/to/splib07a_minerals_beck/ top=20
```

## SEE ALSO

*p.spec.pca*, *p.spec.class*, *p.spectral.planet*, *p.mineral.indices*

## REFERENCES

- Clark, R.N. et al. (2007). USGS Digital Spectral Library splib06a.
  USGS Data Series 231.
- Kokaly, R.F. et al. (2017). USGS Spectral Library Version 7.
  USGS Data Series 1035.
- Kruse, F.A. et al. (1993). The Spectral Image Processing System (SIPS).
  *Remote Sensing of Environment* 44:145–163.
