## DESCRIPTION

*p.shadow.planet* computes a shadow mask for a planetary DEM given the
solar illumination direction. For each surface pixel the algorithm
ray-marches toward the sun along the terrain and tests whether any
intervening topography blocks the solar ray, using the DEM elevation
values.

The sun direction is specified either as solar azimuth and elevation
angles (typically from *p.phocube* or *p.caminfo*) or as a sun vector
in body-fixed Cartesian coordinates.

Output is a binary raster (1 = illuminated, NULL = self-shadowed or
cast-shadowed). An optional **shadow_depth** output raster contains
the length of shadow behind each shadowed pixel.

## NOTES

Ray-marching step size defaults to one DEM pixel. Sub-pixel accuracy
can be increased at the cost of computation time. The maximum shadow
trace distance is bounded by the image diagonal.

OpenMP parallelism is used across rows.

## EXAMPLES

Compute shadow mask using sun angles from p.caminfo:

```sh
p.shadow.planet input=mola_dem \
    sun_azimuth=247.3 sun_elevation=32.1 \
    output=shadow_mask
```

### ISIS3-equivalent workflow

ISIS3 ships a `shadow` application that ray-marches each pixel back
toward the sun and flags those occluded by the DEM. To replicate

```
shade   from=mola.cub  to=mola_shaded.cub \
        azimuth=247.3 zenith=57.9    # zenith = 90 - elevation
shadow  from=mola.cub  to=mola_shadow.cub \
        sunazimuth=247.3 sunzenith=57.9
```

in GRASS, the equivalent chain is:

```sh
# 1. ISIS3 .cub -> GRASS DEM raster
p.in.isis input=mola.cub output=mola_dem
# 2. Shadow mask (replaces ISIS3 shadow)
p.shadow.planet input=mola_dem \
    sun_azimuth=247.3 sun_elevation=32.1 \
    output=mola_shadow
# 3. (optional) export back to ISIS3 .cub
p.out.isis input=mola_shadow output=mola_shadow.cub
```

`mola_shadow` is a binary raster with 1 = shadowed, NULL = sunlit,
matching ISIS3's `shadow.cub` semantics. For a continuous shadow-depth
output (relief shading rather than binary occlusion), add the `-d`
flag.

## REFERENCES

- Oakley, A. (2005). *Planetary topographic mapping.* PhD thesis,
  University of London.

- Wilhelms, D.E. (1987). *The Geologic History of the Moon.*
  USGS Professional Paper 1348.
  doi:[10.3133/pp1348](https://doi.org/10.3133/pp1348)

## SEE ALSO

*[p.dem.prep](p.dem.prep.md),
[p.slope.planet](p.slope.planet.md),
[p.phocube](p.phocube.md),
[r.sunmask](https://grass.osgeo.org/grass-stable/manuals/r.sunmask.html)*

## AUTHOR

Yann Chemin
