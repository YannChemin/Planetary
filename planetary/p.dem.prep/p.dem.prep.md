## DESCRIPTION

*p.dem.prep* prepares a planetary Digital Elevation Model (DEM) for
use in photogrammetry and shape-from-shading workflows. It applies a
sequence of optional preprocessing steps:

1. **NULL filling** — fills data gaps by cubic-spline interpolation
2. **Spike removal** — replaces outlier values exceeding the local
   median by N×σ with the local median
3. **Sphere normalisation** — subtracts the best-fit sphere radius to
   produce a height-above-reference map
4. **Smoothing** — applies a Gaussian low-pass filter to reduce
   high-frequency noise

The output DEM is suitable as input to *p.slope.planet*, *p.shadow.planet*,
and as shape-model data for *p.photomet* via the `p_shapemodel` DEM
callback.

## NOTES

The target body reference radius can be provided directly or looked up
from SPICE PCK kernels when kernels are attached.

## EXAMPLES

Prepare a MOLA gridded DEM for photoclinometry:

```sh
p.dem.prep input=mola_128ppd output=mola_prep \
    radius=3396190.0 smooth_sigma=1.5
```

## REFERENCES

- Beyer, R.A. et al. (2018). The Ames Stereo Pipeline: NASA's open
  source automated stereogrammetry software. *The Planetary Science
  Journal* 2(6):172.
  doi:[10.3847/PSJ/abf3e8](https://doi.org/10.3847/PSJ/abf3e8)

## SEE ALSO

*[p.slope.planet](p.slope.planet.md),
[p.shadow.planet](p.shadow.planet.md),
[r.fill.dir](https://grass.osgeo.org/grass-stable/manuals/r.fill.dir.html),
[r.neighbors](https://grass.osgeo.org/grass-stable/manuals/r.neighbors.html)*

## AUTHOR

Yann Chemin
