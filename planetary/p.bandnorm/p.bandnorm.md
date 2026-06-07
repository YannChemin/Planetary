## DESCRIPTION

*p.bandnorm* normalises each band of a multi-band GRASS imagery group
to a common reference. Each pixel is divided by the mean (or median)
of a user-specified reference band, or by the mean of all bands. The
result preserves spectral shape while removing illumination variations
across the scene.

This step is applied before spectral analysis (e.g., *p.spectral.planet*,
*p.mineral.indices*) to make band ratios and spectral angles insensitive
to absolute brightness variations.

## EXAMPLES

Normalise OMEGA bands to band 10 (1.07 µm continuum):

```sh
p.bandnorm group=omega_cube reference_band=10 \
    output_group=omega_normalized
```

## NOTES

This module is the GRASS equivalent of the ISIS3 *bandnorm* application,
which has long been used in CRISM, OMEGA, and Mariner Mars Coordinated
Lander (MOLA-context) workflows to remove illumination-dependent
brightness variations from multi-band cubes.

## REFERENCES

- Pelkey, S. M., Mustard, J. F., Murchie, S., et al. (2007).
  "CRISM Multispectral Summary Products: Parameterizing Mineral
  Diversity on Mars from Reflectance." *Journal of Geophysical
  Research*, 112(E8), E08S14.
  [doi:10.1029/2006JE002831](https://doi.org/10.1029/2006JE002831)
- Sides, S. H., Becker, T. L., Becker, K. J., et al. (2017).
  "The USGS Integrated Software for Imagers and Spectrometers (ISIS3)
  Instrument Support, New Capabilities, and Releases."
  *Lunar and Planetary Science Conference*, 48, Abstract #2739.
- USGS Astrogeology Science Center. "ISIS3 bandnorm Application."
  [https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/bandnorm/bandnorm.html](https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/bandnorm/bandnorm.html)

## SEE ALSO

*[p.cubenorm](p.cubenorm.md),
[p.spectral.planet](p.spectral.planet.md),
[p.mineral.indices](p.mineral.indices.md)*

## AUTHOR

Yann Chemin
