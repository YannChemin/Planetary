## DESCRIPTION

*p.caminfo* extracts SPICE-derived geometric metadata from a GRASS
raster that has kernels attached via *p.spiceinit* and prints it in
key=value format. It calls `p_spice_geo_row` at the image centre to
derive the complete camera geometry.

Reported quantities:

- Image centre: planetocentric latitude and longitude
- Corner coordinates: lat/lon at each of the four image corners
- Illumination at centre: solar incidence, emission, phase angles
- Sub-solar point: latitude and longitude of the sub-solar point
- Sub-spacecraft point: latitude and longitude below the sensor
- Solar distance: distance from the Sun to the target body (AU)
- Pixel resolution: ground-sample distance at image centre (m/pixel)
- North azimuth: clockwise angle from image up to north

## EXAMPLES

Print geometry for a MRO CTX image:

```sh
p.spiceinit input=ctx_edr ...
p.caminfo input=ctx_edr
```

Save metadata to a file:

```sh
p.caminfo input=hirise_red > hirise_red_caminfo.txt
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65–70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

## NOTES

Requires kernels attached via *p.spiceinit*; the module exits with a SPICE error if no kernel list is found in the mapset. Geometry is evaluated at the image centre only — use *p.phocube* for per-pixel geometry backplanes. The key=value output format is suitable for direct shell parsing with `grep` or `awk`.

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.phocube](p.phocube.md)*

## AUTHOR

Yann Chemin
