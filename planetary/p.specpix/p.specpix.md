## DESCRIPTION

*p.specpix* identifies and replaces special pixel values in a
calibrated planetary raster. ISIS3-derived products may contain
reserved DN values that encode instrument/processing status rather than
physical measurements. These values must be removed before scientific
analysis.

Special pixel types handled:

| Type | ISIS3 name | Typical DN (uint16) |
|---|---|---|
| NULL | Isis::Null | 0 |
| LRS | Low Representation Saturation | 1 |
| LIS | Low Instrument Saturation | 2 |
| HRS | High Representation Saturation | 65534 |
| HIS | High Instrument Saturation | 65535 |

Replacement options: GRASS NULL (default), local neighbourhood mean,
or a fixed user-specified DN.

## EXAMPLES

Replace all special pixels with GRASS NULL:

```sh
p.specpix input=crism_raw output=crism_clean
```

Replace with neighbourhood mean (3×3 window):

```sh
p.specpix input=crism_raw output=crism_clean method=mean size=3
```

## REFERENCES

- ISIS3 Special Pixels. USGS Astrogeology Science Center.
  <https://isis.astrogeology.usgs.gov/Object/Developer/class_isis_1_1_special_pixel.html>

## NOTES

Special-pixel DN thresholds are read from the map's history metadata when populated by *p.in.isis*, and otherwise default to the ISIS3 standard values for uint16 data. For float-valued calibrated products (post-radiometric correction) only the NULL mask is relevant; LRS/LIS/HRS/HIS encoding does not survive the float conversion.

## SEE ALSO

*[p.in.isis](p.in.isis.md),
[r.null](https://grass.osgeo.org/grass-stable/manuals/r.null.html)*

## AUTHOR

Yann Chemin
