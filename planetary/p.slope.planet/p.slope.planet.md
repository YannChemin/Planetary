## DESCRIPTION

*p.slope.planet* computes slope and aspect on a planetary DEM,
accounting for the target body's spherical/ellipsoidal shape. Unlike
*r.slope.aspect*, which assumes a flat (Euclidean) geometry,
*p.slope.planet* corrects for surface curvature using the planet's
equatorial radius **a** and polar radius **b** (computed from
**flattening** = (a−b)/a).

The slope is the angle in degrees between the surface normal and the
local vertical. Aspect is the azimuth of the downslope direction,
measured clockwise from north.

Pixel resolution in metres/pixel must be specified with **ew_res** and
**ns_res** (or a single **res**). For map-projected DEMs these are
typically read from the raster metadata.

## NOTES

A 3×3 central-difference kernel is used. Edge pixels are set to NULL.
For very small bodies (radius < 100 km) the curvature correction is
significant; for Moon-sized and larger bodies it has negligible effect
at typical DEM resolutions.

## EXAMPLES

Compute slope and aspect of Mars MOLA DEM:

```sh
p.slope.planet input=mola_dem \
    a=3396190 flattening=0.00589 res=463 \
    slope=mola_slope aspect=mola_aspect
```

## REFERENCES

- Horn, B.K.P. (1981). Hill shading and the reflectance map.
  *Proceedings of the IEEE* 69(1):14–47.
  doi:[10.1109/PROC.1981.11918](https://doi.org/10.1109/PROC.1981.11918)

- Archinal, B.A. et al. (2018). Report of the IAU WGCCRE: 2015.
  *Celestial Mechanics and Dynamical Astronomy* 130(3):22.
  doi:[10.1007/s10569-017-9805-5](https://doi.org/10.1007/s10569-017-9805-5)

## SEE ALSO

*[p.dem.prep](p.dem.prep.md),
[p.shadow.planet](p.shadow.planet.md),
[r.slope.aspect](https://grass.osgeo.org/grass-stable/manuals/r.slope.aspect.html)*

## AUTHOR

Yann Chemin
