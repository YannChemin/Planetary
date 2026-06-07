## DESCRIPTION

*p.in.pds4* imports Array_2D_Image and Array_3D_Image data objects from
a PDS4 XML-labelled planetary product into GRASS raster maps. The XML
label file (`.xml`) is parsed to locate the binary data file and the
array axes (lines × samples × bands). Calibration via `offset` and
`scaling_factor` XML attributes is applied:

```
physical = offset + DN × scaling_factor
```

Multi-band (Array_3D_Image) products produce maps named **output.1**,
**output.2**, etc. The **-g** flag registers them in an imagery group.

## NOTES

PDS4 uses UTF-8 XML labels. The internal XML parser handles nested
`File_Area_Observational` and `Array_2D_Image` / `Array_3D_Image`
elements as defined in the PDS4 Information Model.

Byte order is taken from the `<Element_Array><data_type>` field;
`IEEE754MSBSingle`, `IEEE754MSBDouble`, `SignedMSB2`, `UnsignedMSB2`
etc. are supported.

## EXAMPLES

Import a MESSENGER MDIS NAC PDS4 product:

```sh
p.in.pds4 input=EW0211981.xml output=mdis_nac
```

Import a multi-band Europa Clipper EIS product:

```sh
p.in.pds4 -g input=eis_nac_20300101.xml output=eis_cube
```

## REFERENCES

- PDS4 Standards Reference, Version 1.18. JPL D-96008 (2021).
  <https://pds.nasa.gov/datastandards/pds4/>

- PDS4 Information Model Specification. NASA.
  <https://pds.nasa.gov/pds4/pds/v1/>

## SEE ALSO

*[p.in.pds3](p.in.pds3.md),
[p.in.isis](p.in.isis.md),
[r.in.gdal](https://grass.osgeo.org/grass-stable/manuals/r.in.gdal.html)*

## AUTHOR

Yann Chemin
