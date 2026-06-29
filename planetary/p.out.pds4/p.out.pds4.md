## DESCRIPTION

*p.out.pds4* exports a GRASS raster map as a PDS4 data product: a GeoTIFF
data file and a companion XML label conforming to the PDS4 Information
Model (IM 1.21) with the PDS4 Cartography (cart) local data dictionary.

The GeoTIFF is written with a planetary-body CRS (PROJ spherical lat/lon
using the body's equatorial and polar radii from the bodies/ JSON database).
The XML label records the bounding coordinates, pixel resolution, geodetic
model, and — when a `planetary.json` metadata sidecar is present for the
input map — the sensor, mission, and observational time.

## OUTPUT FILES

Two files are written, sharing the same base path:

- **`<output>.tif`** — GeoTIFF raster, DEFLATE-compressed, tiled.
  Data type selected by `type=` (default Float32).
  NODATA = NaN for floating-point; −9999 for integer types.
- **`<output>.xml`** — PDS4 `Product_Ancillary` label referencing the TIF.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `input=` | required | GRASS raster map to export |
| `output=` | required | Output file base path (no extension) |
| `body=` | mars | Target planetary body (radii source) |
| `title=` | auto | Product title for the PDS4 label |
| `lid=` | auto | PDS4 Logical Identifier (after `urn:nasa:pds:`) |
| `type=` | Float32 | GDAL data type (Byte, Int16, UInt16, Int32, Float32, Float64) |

## NOTES

- `body=` must match a file in `$GISBASE/bodies/` (e.g. `mars`, `moon`,
  `mercury`, `venus`, `titan`, `ceres`, `enceladus`, `europa`).
- The PDS4 LID is auto-generated from the map name when `lid=` is not set.
  For archival submissions supply a valid LID following your archive's
  bundle/collection naming convention.
- Requires `python3-gdal` (`from osgeo import gdal, osr`) for the
  planetary CRS injection step.  Without it the GeoTIFF is still written
  (with the GRASS location CRS), but a warning is issued.
- The `cart:Cartography` block assumes a geographic (lat/lon) output
  region.  Projected outputs (sinusoidal, stereo, etc.) are written
  correctly as GeoTIFF but the PDS4 `cart:Geographic` block will show
  pixel sizes in degree-equivalent units — edit the label manually for
  projected submittals.

## EXAMPLES

Export a CRISM-derived olivine band depth map for Mars:

```sh
p.out.pds4 input=crism_bd_olivine output=/data/archive/crism_bd_olivine \
    body=mars title="CRISM olivine band depth, Nili Fossae"
```

Export a Moon M3 IBD1000 map:

```sh
p.out.pds4 input=m3_ibd1000 output=/data/archive/m3_ibd1000 \
    body=moon title="M3 IBD1000 — Sinus Iridum region"
```

## SEE ALSO

*p.in.pds4*, *p.in.pds3*, *p.mineral.indices*, *r.out.gdal*
