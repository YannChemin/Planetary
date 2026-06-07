## DESCRIPTION

*p.mineral.indices* computes standard CRISM-derived planetary mineral
and compositional spectral summary parameters from a multi-band imagery
group. Each index is computed from specific wavelength bands and written
to a separate output raster.

Available indices (based on Pelkey et al. 2007 and Viviano-Beck et al. 2014):

| Index | Bands used | Sensitivity |
|---|---|---|
| OLINDEX | 1.21, 1.31, 1.50 µm | Olivine |
| LCPINDEX | 1.81, 2.07, 2.30 µm | Low-Ca pyroxene |
| HCPINDEX | 2.14, 2.21, 2.45 µm | High-Ca pyroxene |
| D2300 | 2.12, 2.30, 2.53 µm | Carbonates / Mg-OH |
| BD1900 | 1.85, 1.93, 2.07 µm | Water of hydration |
| BD3000 | 2.53, 3.00, 3.52 µm | H₂O ice / hydrated minerals |

Band wavelengths for each index are specified by providing band numbers
in the imagery group (sorted by wavelength).

## EXAMPLES

Compute all six indices for a CRISM FRT product:

```sh
p.mineral.indices group=crism_trdr \
    olindex=crism_olindex lcpindex=crism_lcpindex \
    hcpindex=crism_hcpindex d2300=crism_d2300
```

## REFERENCES

- Pelkey, S.M. et al. (2007). CRISM multispectral summary products.
  *J. Geophys. Res.* 112(E8):E08S14.
  doi:[10.1029/2006JE002831](https://doi.org/10.1029/2006JE002831)

- Viviano-Beck, C.E. et al. (2014). Revised CRISM spectral parameters
  based on the currently detected mineral diversity on Mars.
  *J. Geophys. Res. Planets* 119(6):1403–1431.
  doi:[10.1002/2014JE004627](https://doi.org/10.1002/2014JE004627)

## NOTES

All input bands must be in calibrated reflectance (I/F) units normalised to [0, 1]. Band-to-wavelength matching uses the wavelength stored in each band's history metadata (set by *p.in.isis* or *p.in.pds*); the module does not match by band number. CRISM FRT and HRL products imported via *p.in.pds* already carry the required wavelength metadata.

## SEE ALSO

*[p.spectral.planet](p.spectral.planet.md),
[p.bandnorm](p.bandnorm.md)*

## AUTHOR

Yann Chemin
