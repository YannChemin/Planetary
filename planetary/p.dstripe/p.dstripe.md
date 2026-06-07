## DESCRIPTION

*p.dstripe* removes detector stripe noise from a planetary raster.
Striping arises from detector-to-detector gain and offset differences
in line-scan or push-broom sensors. The algorithm estimates the stripe
pattern as the column-wise (or row-wise) mean or median across all
lines, then subtracts it from each line.

A low-pass filter can be applied to the estimated stripe profile before
subtraction to preserve large-scale brightness gradients while still
removing the high-frequency periodic noise.

Destriping direction: **columns** (along-track striping, default) or
**rows** (across-track striping).

## EXAMPLES

Destripe a THEMIS VIS image (column striping):

```sh
p.dstripe input=themis_vis_raw direction=columns \
    estimator=median output=themis_vis_destriped
```

## REFERENCES

- Crisp, D. et al. (1991). The dark side of Venus: Near-infrared
  images from the Anglo-Australian Observatory. *Science*
  253:1263–1266.
  doi:[10.1126/science.253.5025.1263](https://doi.org/10.1126/science.253.5025.1263)

- Wegener, M. (1991). Destriping multisensor imagery. *International
  Journal of Remote Sensing* 12(7):1601–1603.

## NOTES

The stripe profile is estimated from the median (or mean) column statistics before subtraction. For images with strong large-scale brightness gradients, set `sigma` to at least twice the expected stripe-free gradient wavelength to avoid subtracting real signal. Apply *p.cubenorm* for gain-and-offset correction of multi-band cubes before running *p.dstripe*.

## SEE ALSO

*[p.desmear](p.desmear.md),
[p.cubenorm](p.cubenorm.md),
[r.neighbors](https://grass.osgeo.org/grass-stable/manuals/r.neighbors.html)*

## AUTHOR

Yann Chemin
