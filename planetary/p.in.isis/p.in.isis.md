## DESCRIPTION

*p.in.isis* imports an ISIS3 cube file (`.cub`) into one or more GRASS
raster maps. ISIS3 cubes store a 512-byte (or multiple thereof) PVL
label block followed by pixel data in BSQ, BIL, or BIP organisation.
All ISIS3 pixel types are supported: UnsignedByte, SignedWord,
UnsignedWord, SignedInteger, Real (32-bit float), Double (64-bit float).

The five ISIS3 special pixel values are mapped to GRASS NULL:

| ISIS3 special | Meaning |
|---|---|
| NULL | Missing or invalid DN |
| LRS | Low representation saturation |
| LIS | Low instrument saturation |
| HRS | High representation saturation |
| HIS | High instrument saturation |

Multi-band cubes produce maps named **output.1**, **output.2**, etc.
The **-g** flag registers them in a GRASS imagery group.

## NOTES

SPICE-attached cube geometry (CameraStatistics, Mapping groups in the
ISIS3 label) is extracted and stored in the output raster's metadata
when available.

## EXAMPLES

Import an ISIS3 HiRISE RED cube:

```sh
p.in.isis input=PSP_001777_1650_RED.cub output=hirise_red
r.info hirise_red
```

Import a CTX cube and set the region to match:

```sh
p.in.isis input=ctx_edr.cub output=ctx_image
g.region raster=ctx_image
```

## REFERENCES

- ISIS3 Cube Format. USGS Astrogeology Science Center.
  <https://isis.astrogeology.usgs.gov/Object/Developer/class_isis_1_1_cube.html>

- Sides, S.C. et al. (2017). The USGS Integrated Software for Imagers
  and Spectrometers (ISIS 3). *Lunar and Planetary Science Conference*
  48, abstract 2739.

## SEE ALSO

*[p.in.pds3](p.in.pds3.md),
[p.out.isis](p.out.isis.md),
[p.spiceinit](p.spiceinit.md)*

## AUTHOR

Yann Chemin
