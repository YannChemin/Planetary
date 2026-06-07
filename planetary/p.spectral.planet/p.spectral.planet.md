## DESCRIPTION

*p.spectral.planet* computes spectral analysis products from a
multi-band GRASS imagery group. Supported operations:

- **ratio** — band ratio: output = band_a / band_b
- **ndindex** — normalised difference index: (band_a − band_b) / (band_a + band_b)
- **sam** — spectral angle mapper: angle between pixel spectrum and reference spectrum
- **depth** — band depth relative to a linear continuum across three bands
- **continuum** — continuum removal (divide band by linear continuum)

For **sam** the reference spectrum is read from a text file with one
reflectance value per line (one per group band).

Uses the `p_spectra` library (band_depth, sam, continuum_remove functions).

## EXAMPLES

Compute a spectral angle map against a pyroxene reference spectrum:

```sh
p.spectral.planet group=crism_trdr operation=sam \
    spectrum=pyroxene_spectrum.txt output=crism_sam
```

Compute a band ratio (CRISM band 73 / band 23):

```sh
p.spectral.planet group=crism_trdr operation=ratio \
    band_a=73 band_b=23 output=crism_ratio_73_23
```

## REFERENCES

- Clark, R.N. & Roush, T.L. (1984). Reflectance spectroscopy:
  Quantitative analysis techniques for remote sensing applications.
  *J. Geophys. Res.* 89(B7):6329–6340.
  doi:[10.1029/JB089iB07p06329](https://doi.org/10.1029/JB089iB07p06329)

- Kruse, F.A. et al. (1993). The Spectral Image Processing System
  (SIPS). *Remote Sensing of Environment* 44(2–3):145–163.
  doi:[10.1016/0034-4257(93)90013-N](https://doi.org/10.1016/0034-4257(93)90013-N)

## NOTES

For `method=sam`, the reference spectrum must have the same number of bands as the input group in the same order; the output angle map is in radians. For `method=depth`, the three wavelength-index parameters (band_a, band_c1, band_c2) identify the absorption centre and continuum anchor points by band position (1-based), not by wavelength value.

## SEE ALSO

*[p.mineral.indices](p.mineral.indices.md),
[p.specpix](p.specpix.md),
[p.bandnorm](p.bandnorm.md)*

## AUTHOR

Yann Chemin
