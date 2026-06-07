## DESCRIPTION

*p.cam2map* reprojects a raw spacecraft camera image (sensor/camera
geometry) into a cartographically projected GRASS raster using SPICE
geometry attached by *p.spiceinit*. For each output pixel the
back-projection computes the ground intercept via NAIF `sincpt`, maps
it through the selected map projection, and bilinearly interpolates
the input DN value.

Supported projections: Sinusoidal, Simple Cylindrical, Equirectangular,
Polar Stereographic, Orthographic, Mercator, Lambert Conformal Conic,
Transverse Mercator.

Output resolution may be auto-computed from the camera pixel scale at
image centre or set explicitly with the **res** parameter (in
degrees/pixel or metres/pixel depending on projection).

## NOTES

*p.cam2map* uses `p_projection_planet` for the forward/inverse
projection math and NAIF SPICE for camera-model ray-tracing. Processing
is row-parallel with OpenMP.

## EXAMPLES

Project a CTX EDR to Simple Cylindrical at 6 m/pixel:

```sh
p.spiceinit input=ctx_edr ...
p.cam2map input=ctx_edr projection=simplecylindrical \
    res=0.0001 output=ctx_projected
```

Polar-stereographic projection for a HiRISE polar image:

```sh
p.cam2map input=hirise_polar projection=polarstereographic \
    clon=0 output=hirise_ps
```

## REFERENCES

- Snyder, J.P. (1987). *Map Projections — A Working Manual.*
  USGS Professional Paper 1395.
  doi:[10.3133/pp1395](https://doi.org/10.3133/pp1395)

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65–70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.caminfo](p.caminfo.md),
[r.proj](https://grass.osgeo.org/grass-stable/manuals/r.proj.html)*

## AUTHOR

Yann Chemin
