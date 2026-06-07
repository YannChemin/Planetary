## DESCRIPTION

*p.rings.project* reprojects a planetary ring-plane image from sensor
geometry into ring-plane (radius, longitude) coordinates using one of
three ring-plane map projections from the `p_projection_planet` library:

| Projection | Description |
|---|---|
| **RingCylindrical** | Radius vs. longitude (linear both axes) |
| **LunarAzimuthalEA** | Azimuthal equal-area for ring-plane |
| **UpturnedTA** | Upturned ellipsoid transverse azimuthal (Newton-Raphson inverse) |

Input: a sensor-geometry ring image with SPICE kernels attached via
*p.spiceinit*. Output: ring-plane GRASS raster.

Parameters: **rmin**, **rmax** (ring radius range, km), **lon_min**,
**lon_max** (longitude range, degrees), **res** (output resolution,
km/pixel).

## EXAMPLES

Project a Cassini ISS ring image to ring-cylindrical coordinates:

```sh
p.spiceinit input=cassini_iss_ring ...
p.rings.project input=cassini_iss_ring \
    projection=RingCylindrical \
    rmin=74500 rmax=140220 res=1 \
    output=saturn_rings_projected
```

## REFERENCES

- French, R.G. et al. (1993). Geometry of the Saturn system from the
  3 July 1989 occultation of 28 Sgr. *Icarus* 103(2):163–214.
  doi:[10.1006/icar.1993.1066](https://doi.org/10.1006/icar.1993.1066)

- Porco, C.C. et al. (2005). Cassini Imaging Science: Initial results
  on Saturn's rings. *Science* 307(5713):1226–1236.
  doi:[10.1126/science.1108056](https://doi.org/10.1126/science.1108056)

## NOTES

The Newton-Raphson inversion in the UpturnedTA projection may diverge for pixels very close to the ring-plane edge; such pixels are set to NULL. Maximum iteration count and convergence tolerance can be set via `maxiter=` and `eps=`. All three projections assume a flat, infinitely thin ring plane.

## SEE ALSO

*[p.rings.stats](p.rings.stats.md),
[p.spiceinit](p.spiceinit.md),
[p.cam2map](p.cam2map.md)*

## AUTHOR

Yann Chemin
