## DESCRIPTION

**p.in.dem** imports one or more planetary DEM files into a GRASS GIS raster
map, handling multi-tile mosaicking, void filling, and optional resampling.
It is the recommended starting point for setting up a GRASS location for
planetary landing-site analysis.

Supported input formats: PDS3 `.lbl`/`.img`, ISIS3 `.cub`, GeoTIFF, and any
format readable by GDAL via *p.in.pds*.

### Multi-tile mosaicking

When `input=` resolves to more than one file (comma-separated list or shell
glob) the tiles are imported individually and merged with *r.patch*. Temporary
per-tile maps are removed unless the **-k** flag is set.

### Void filling

The **-f** flag applies *r.fill.stats* after mosaicking to interpolate small
NULL regions (radar shadows, data gaps). Large data voids are not
extrapolated.

### Unit conversion

Inherited from *p.in.pds*: if the PDS label reports `UNIT = KILOMETER` the
raster values are multiplied by 1000 so the output is always in **metres**.

## PARAMETERS

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `input` | file(s) | *required* | DEM file(s): single path, comma-separated list, or glob |
| `output` | raster | *required* | Output DEM raster map name |
| `body` | file | — | Body descriptor JSON (provides radius and default projection) |
| `resolution` | float | native | Target resolution in metres |
| `resample` | string | `bilinear_f` | Resampling method: nearest, bilinear, bicubic, lanczos, bilinear_f, bicubic_f |
| `memory` | integer | 300 | Maximum memory for r.import in MB |

## FLAGS

| Flag | Description |
|------|-------------|
| `-f` | Fill null/void pixels after import using r.fill.stats |
| `-r` | Set computational region to match the output DEM |
| `-k` | Keep intermediate per-tile rasters |

## EXAMPLES

```bash
# Import a single SLDEM2015 tile and set region
p.in.dem input=SLDEM2015_128_60S_60N_000_360_FLOAT.LBL output=sldem128 -r

# Mosaic two LOLA polar tiles and fill voids
p.in.dem input="LOLA_5M_NPole_90_180.tif,LOLA_5M_NPole_0_90.tif" \
         output=lola_5m -f -r

# Import at 30 m working resolution
p.in.dem input=lola_polar_dem.cub output=lola_30m resolution=30 -r
```

## NOTES

For multi-tile mosaics, GDAL's VRT mechanism is used in memory; very large tile sets (> 100 tiles, > 10 GB uncompressed) may require increasing `GDAL_CACHEMAX`. Void filling uses *r.fill.stats* with inverse-distance weighting; large voids (> 100 pixels across) may require repeated passes or manual interpolation.

## SEE ALSO

*[p.in.pds](p.in.pds.md),
[p.in.ancillary](p.in.ancillary.md),
[r.import](https://grass.osgeo.org/grass-stable/manuals/r.import.html),
[r.patch](https://grass.osgeo.org/grass-stable/manuals/r.patch.html),
[r.fill.stats](https://grass.osgeo.org/grass-stable/manuals/addons/r.fill.stats.html)*

## REFERENCES

- Smith et al. (2010) The Lunar Orbiter Laser Altimeter Investigation on the
  Lunar Reconnaissance Orbiter Mission. *Space Science Reviews* 150, 209–241.
- Barker et al. (2016) A new lunar digital elevation model from the Lunar
  Orbiter Laser Altimeter. *Icarus* 273, 346–355.

## AUTHOR

Yann Chemin
