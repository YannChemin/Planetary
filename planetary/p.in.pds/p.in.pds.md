## DESCRIPTION

**p.in.pds** imports a planetary data file in PDS3, PDS4, or ISIS3 cube
format into a GRASS GIS raster map. It is the primary data-ingestion tool
of the `p.*` planetary landing-site evaluation toolkit.

The module resolves the input format and chooses the most direct import path:

1. **GDAL fast path** (preferred): GDAL 3.x includes native drivers for PDS3
   (`PDS`), PDS4 (`PDS4`), ISIS3 cubes (`ISIS3`), and ISIS2 cubes (`ISIS2`).
   When GDAL can open the file the module calls *r.import* directly,
   preserving float32 precision and applying the CRS embedded in the label.

2. **ISIS3 fallback path**: If GDAL cannot open the file (uncommon with
   GDAL ≥ 3.4) or if the **-c** flag is set to project raw camera data, the
   module runs the ISIS3 pipeline:
   `pds2isis` → (optionally `cam2map`) → GDAL ISIS3 driver → *r.import*.

### Companion label files (.LBL)

PDS3 binary images (`.img`, `.IMG`) carry their metadata in a separate ASCII
label file (`.lbl`, `.LBL`). GDAL's PDS driver reads the label, not the
binary image. When the user supplies an `.img` filename the module looks for
the companion `.lbl` automatically (same base name, same directory).

### Unit conversion

Several standard planetary DEMs express height in kilometres (SLDEM2015,
LOLA RDR, MOLA GDR). **p.in.pds** reads the `UNIT` keyword from the PDS
label and multiplies raster values by 1000 so that the output is always in
**metres**. Set `scale=1` to suppress auto-scaling.

### ISIS3 environment

The ISIS3 fallback requires the `pds2isis` (and optionally `cam2map`)
executables. The module searches for them in `$ISISROOT/bin` then `$PATH`.
Override with the `isis3=` parameter.

## PARAMETERS

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `input` | file | *required* | Input PDS3 .lbl/.img, ISIS3 .cub, or PDS4 .xml file |
| `output` | raster | *required* | Name for the output GRASS raster map |
| `band` | integer | 1 | Band number for multi-band products |
| `scale` | float | 0 (auto) | Multiplicative scale factor; 0 = auto-detect from label UNIT keyword |
| `target` | string | — | Target body name for ISIS3 cam2map (e.g. MOON) |
| `isis3` | path | — | Path to ISIS3 bin directory if not in PATH |

## FLAGS

| Flag | Description |
|------|-------------|
| `-c` | Force ISIS3 cam2map projection of raw camera data |
| `-k` | Keep intermediate files (ISIS cube, etc.) for debugging |
| `-r` | Set computational region to match the imported raster |

## NOTES

The GRASS location must already use the correct planetary body CRS before
importing. Use *p.in.dem* to set up the location from a body-descriptor JSON,
or create it manually with *g.proj*.

Multi-band products (e.g., multi-spectral ISIS cubes) are imported one band
at a time using the `band=` parameter.

## EXAMPLES

```bash
# Import SLDEM2015 (PDS3 .LBL/.IMG, km → m auto-conversion)
p.in.pds input=SLDEM2015_128_60S_60N_000_360_FLOAT.LBL output=sldem128

# Import a .IMG whose companion .LBL is in the same directory
p.in.pds input=SLDEM2015_256_60S_60N_000_360_FLOAT.IMG output=sldem256

# Import a single-band ISIS3 cube
p.in.pds input=lola_polar_dem.cub output=lola_spole

# Import band 2 of a multi-band ISIS3 cube
p.in.pds input=kaguya_sp.cub band=2 output=kaguya_feo

# Import raw LRO NAC image and project it (cam2map)
p.in.pds -c input=M123456789L.img output=lronac_ortho target=MOON

# Import MOLA GDR without km → m conversion
p.in.pds input=megt90n000fb.lbl output=mola_dem scale=1
```

## SEE ALSO

*[p.in.dem](p.in.dem.md),
[p.in.ancillary](p.in.ancillary.md),
[r.import](https://grass.osgeo.org/grass-stable/manuals/r.import.html),
[r.in.gdal](https://grass.osgeo.org/grass-stable/manuals/r.in.gdal.html)*

## REFERENCES

- Barker et al. (2016) A new lunar digital elevation model from the Lunar
  Orbiter Laser Altimeter. *Icarus* 273, 346–355.
- ISIS3 documentation: <https://isis.astrogeology.usgs.gov/>
- GDAL PDS/ISIS drivers: <https://gdal.org/drivers/raster/pds.html>

## AUTHOR

Yann Chemin
