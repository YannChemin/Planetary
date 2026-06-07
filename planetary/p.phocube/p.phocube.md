## DESCRIPTION

*p.phocube* computes per-pixel photometric geometry backplane rasters
from a GRASS raster map that has SPICE kernels attached (via
*p.spiceinit*). For each output band selected by flags, a separate GRASS
raster is written named **output_bandname**.

Available backplane bands:

| Flag | Band name | Description |
|---|---|---|
| -i | incidence | Solar incidence angle at surface (degrees) |
| -e | emission | Emission angle to sensor (degrees) |
| -g | phase | Phase angle Sun–surface–sensor (degrees) |
| -l | latitude | Planetocentric latitude (degrees) |
| -o | longitude | West longitude (degrees) |
| -r | pixres | Ground-sample distance (metres/pixel) |
| -n | northaz | North azimuth (degrees) |
| -s | sunaz | Sun azimuth (degrees) |

All angles are computed using NAIF SPICE `sincpt` (surface intercept)
and `ilumin` (illumination angles) routines. The target body shape is
taken from the PCK/DSK kernels loaded via *p.spiceinit*.

## NOTES

*p.phocube* is the essential precursor to *p.photomet*, *p.photrim*,
and *p.albedo*: photometric correction requires per-pixel i, e, g maps.

Processing is row-parallel with OpenMP via `p_spice_geo_row`. Because
CSPICE internal state is not thread-safe, SPICE calls are serialised
within the row function while only the pure-math angle arithmetic is
parallelised.

## EXAMPLES

Compute incidence, emission, and phase angles for a HiRISE image:

```sh
p.spiceinit input=hirise_red lsk=naif0012.tls ...
p.phocube -ieg input=hirise_red output=hirise
# produces: hirise_incidence, hirise_emission, hirise_phase
```

Photometric correction pipeline:

```sh
p.phocube -ieg input=ctx output=ctx_geom
p.photomet input=ctx incidence=ctx_geom_incidence \
    emission=ctx_geom_emission phase=ctx_geom_phase \
    model=LunarLambert output=ctx_phocorr
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65–70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

- Hapke, B. (1981). Bidirectional reflectance spectroscopy 1. Theory.
  *J. Geophys. Res.* 86(B4):3039–3054.
  doi:[10.1029/JB086iB04p03039](https://doi.org/10.1029/JB086iB04p03039)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.photomet](p.photomet.md),
[p.photrim](p.photrim.md),
[p.caminfo](p.caminfo.md)*

## AUTHOR

Yann Chemin
