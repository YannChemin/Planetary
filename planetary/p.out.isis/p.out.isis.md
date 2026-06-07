## DESCRIPTION

*p.out.isis* exports one or more GRASS raster maps to an ISIS3 cube
file (`.cub`). A minimal PVL label is written (512-byte block) followed
by pixel data in BSQ organisation with the pixel type chosen to best
preserve the GRASS DCELL value range without overflow.

When exporting a single raster the output cube is single-band. When
exporting an imagery group (with the **group** option) the cube is
multi-band with bands ordered by the group registration sequence.

The output cube is readable directly by ISIS3 applications such as
*qview*, *catlab*, *spiceinit*, and *cam2map*.

## NOTES

Default output pixel type is 32-bit IEEE float (Real). Use **type=s16**
for signed 16-bit integer output if the data range permits, which halves
file size.

Scaling (`SCALING_FACTOR`, `OFFSET`) is written to the ISIS3 label when
the input raster has non-unit calibration parameters.

## EXAMPLES

Export a photometrically corrected raster to ISIS3 cube:

```sh
p.out.isis input=photomet_result output=photomet.cub
```

Export a multi-band group to ISIS3 cube:

```sh
p.out.isis group=crism_corrected output=crism_out.cub
```

## REFERENCES

- ISIS3 Cube Format. USGS Astrogeology Science Center.
  <https://isis.astrogeology.usgs.gov/>

## SEE ALSO

*[p.in.isis](p.in.isis.md),
[p.in.pds3](p.in.pds3.md),
[r.out.gdal](https://grass.osgeo.org/grass-stable/manuals/r.out.gdal.html)*

## AUTHOR

Yann Chemin
