## DESCRIPTION

*p.cubenorm* normalises a raster by dividing each line (or sample) by
its statistical summary (mean or median) and multiplying by the overall
image mean. This removes cross-track or along-track sensitivity
variations caused by detector-to-detector differences that survive
flat-field calibration.

Normalisation direction: **lines** (per-line normalisation, removes
along-track variations) or **samples** (per-sample, removes
cross-track variations).

## NOTES

For multi-band data, apply *p.cubenorm* independently to each band of
the imagery group or use the **group** input option to process all
bands at once.

## EXAMPLES

Normalise CRISM bands per-sample to remove across-track brightness
gradients:

```sh
p.cubenorm input=crism_band73 direction=samples \
    estimator=median output=crism_band73_norm
```

## REFERENCES

- Eliason, E. M. (1992). "Production of digital image models using the
  ISIS system." *Lunar and Planetary Science Conference*, 23, 331-332.
- Becker, K. J., Anderson, J. A., Sides, S. H., et al. (2013). "Cassini
  ISS Geometric Calibration of the Wide Angle Camera." *Lunar and
  Planetary Science Conference*, 44, Abstract #2845.
- Sides, S. H., Becker, T. L., Becker, K. J., et al. (2017). "The USGS
  Integrated Software for Imagers and Spectrometers (ISIS3) Instrument
  Support, New Capabilities, and Releases." *Lunar and Planetary
  Science Conference*, 48, Abstract #2739.
- USGS Astrogeology Science Center. "ISIS3 cubenorm Application."
  [https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/cubenorm/cubenorm.html](https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/cubenorm/cubenorm.html)

## SEE ALSO

*[p.dstripe](p.dstripe.md),
[p.bandnorm](p.bandnorm.md)*

## AUTHOR

Yann Chemin
