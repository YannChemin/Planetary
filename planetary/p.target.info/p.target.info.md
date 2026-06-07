## DESCRIPTION

*p.target.info* prints physical and geometric properties of a planetary
target body using SPICE PCK (Planetary Constants Kernel) data. For a
given NAIF body name or ID, the module reads the PCK kernel and reports:

- Tri-axial radii (a, b, c) in km
- Mean radius and volumetric mean radius
- Gravitational parameter GM (km³/s²)
- Rotation period (hours)
- Reference frame name
- IAU working-group report year

Optionally writes the properties to a GRASS raster's history metadata.

## EXAMPLES

Print properties of Mars using pck00010.tpc:

```sh
p.target.info target=MARS pck=pck00010.tpc
```

Write Moon properties to a raster's metadata:

```sh
p.target.info target=MOON pck=pck00010.tpc \
    attach_to=lola_dem
```

## REFERENCES

- Archinal, B.A. et al. (2018). Report of the IAU Working Group on
  Cartographic Coordinates and Rotational Elements: 2015.
  *Celestial Mechanics and Dynamical Astronomy* 130(3):22.
  doi:[10.1007/s10569-017-9805-5](https://doi.org/10.1007/s10569-017-9805-5)

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65–70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

## NOTES

PCK data coverage varies by body; body constants for recent missions are in `pck00011.tpc` (Cassini/Juno era). If the requested body is not in the loaded kernels the module exits with a SPICE error. Standard NAIF body IDs are used (Moon = 301, Mars = 499, Titan = 606); named aliases are resolved via the loaded FK kernel.

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.caminfo](p.caminfo.md)*

## AUTHOR

Yann Chemin
