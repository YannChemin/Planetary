## DESCRIPTION

*p.in.pds3* imports one or more bands from a PDS3 planetary image product
into GRASS raster maps. It reads the PVL label (attached or detached),
locates the pixel data using the `^IMAGE`, `^QUBE`, or `^SPECTRAL_QUBE`
pointer, and writes one **DCELL** (64-bit floating-point) GRASS raster
per band after applying the calibration scaling:

```
physical = OFFSET + DN × SCALING_FACTOR
```

Supported pixel types: UINT8, INT16, UINT16, INT32, UINT32, FLOAT32,
FLOAT64. Band storage orders BSQ, BIL, and BIP are handled. Special
ISIS3 DN values (`CORE_NULL`, `MISSING_CONSTANT`) are mapped to GRASS
NULL.

For single-band products the output map is named **output**. For
multi-band products the maps are named **output.1**, **output.2**, etc.
The optional **-g** flag registers all output maps in a GRASS imagery
group of the same name.

If no computational region has been set (default 1 × 1 XY region),
*p.in.pds3* sets the region to match the image dimensions with 1:1
pixel coordinates before writing.

### Detached labels

When the input is a `.lbl` file (detached PDS3 label), *p.in.pds3*
automatically locates the companion binary data file by searching for
files with the same base name and extensions `.img`, `.IMG`, `.dat`,
`.DAT`, `.fit`, `.FIT` in the same directory.

### Stale ^IMAGE offsets

Cropped or re-labelled PDS3 files sometimes carry `^IMAGE = N <BYTES>`
pointers that still point into the ASCII label area. *p.in.pds3*
detects this condition and scans forward to find the true start of
binary pixel data.

## NOTES

The module uses `Rast_set_window` / `Rast_get_window` (not
`G_set_window`) to ensure the GRASS raster window is consistent with
the output dimensions.

OpenMP parallelism is used when reading multi-band QUBE products.

## EXAMPLES

Import a MOC-WA Mars image (attached label, UINT8):

```sh
p.in.pds3 input=ab102401.img output=moc_wa
r.info moc_wa
```

Import a LOLA lunar DEM (nested label, INT16, calibrated to metres):

```sh
p.in.pds3 input=ldem_4.img output=lola_dem
r.univar lola_dem
```

Import a Magellan Venus radar image (detached label):

```sh
p.in.pds3 input=ff17.lbl output=magellan_venus
```

Import a multi-band CRISM cube and register in an imagery group:

```sh
p.in.pds3 -g input=frt00003e12_07_if166l_trr3.img output=crism_trdr
i.group -l group=crism_trdr
```

## REFERENCES

- Planetary Data System Standards Reference, Rev. B. JPL D-7669 (2009).
  <https://pds.nasa.gov/datastandards/pds3/standards/>

- Sides, S.C. et al. (2017). The USGS Integrated Software for Imagers
  and Spectrometers (ISIS 3) instrument support, new capabilities, and
  release history. *Lunar and Planetary Science Conference* 48, abstract
  2739.

## SEE ALSO

*[p.in.pds4](p.in.pds4.md),
[p.in.isis](p.in.isis.md),
[p.out.isis](p.out.isis.md),
[p.spiceinit](p.spiceinit.md),
[r.in.gdal](https://grass.osgeo.org/grass-stable/manuals/r.in.gdal.html)*

## AUTHOR

Yann Chemin
