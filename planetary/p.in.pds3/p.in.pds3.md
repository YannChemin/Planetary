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

### Labels describing more than one image object

Some archives' attached labels describe several image objects side by
side rather than just one — e.g. JPL PDS Imaging Node M3 L1B products,
whose single label carries `RDN_IMAGE` (radiance), `LOC_IMAGE`
(longitude/latitude/radius), and `OBS_IMAGE` (illumination/viewing
geometry), each pointing at its own companion `.IMG` file. By default
*p.in.pds3* imports the first matching object found (`IMAGE`, `QUBE`,
`SPECTRAL_QUBE`, or the first `*_IMAGE`/`*_QUBE` fallback match). Use
**object=** to select a specific one by its exact PDS3 object name
(the name used in its `^NAME` pointer keyword), e.g.
`object=LOC_IMAGE`.

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

### Data pointers nested inside `OBJECT = FILE`

Some PDS3 archives (e.g. MRO/CRISM Targeted RDR) wrap multiple data
objects sharing one physical file inside an enclosing
`OBJECT = FILE ... END_OBJECT = FILE` block, placing pointer keywords
such as `^IMAGE` one level below the label root rather than at the top
level. The label parser performs a recursive depth-first search for
these pointer keywords (and for `RECORD_BYTES`) so both flat and
`OBJECT = FILE`-wrapped labels resolve correctly. A label whose pointer
keyword cannot be found at all (instead of merely being nested) falls
back to reading the label file itself as pixel data and will fail
loudly with read errors past the label's own (small) size — it does not
silently produce garbage.

### Null/special-pixel defaults

When a product label declares neither `CORE_NULL` nor
`MISSING_CONSTANT`, the null DN is **not** defaulted to `0.0`: doing so
would cause every legitimate near-zero sample (common across
reflectance/I-F products) to be misclassified as a null pixel. Instead
no DN is treated as special in that case, and the caller is responsible
for masking any sensor-specific sentinel the label does not declare
(e.g. CRISM's undeclared `65535.0` bad/saturated-pixel flag — see the
`r.mapcalc` masking step in `p.in.archive`'s CRISM examples and the
Mars-mineralogy chapter pipeline referenced there).

### QUBE band-suffix ("backplane") sideplanes

Some real PDS3 QUBE products (e.g. Cassini VIMS, Mars Express OMEGA)
append extra "band-suffix" rows once per line, after all real bands --
per-line housekeeping/calibration sideplanes, not spectral data. For
example, MEX OMEGA's band-suffix index 1 holds the real scanning mirror
position (DN) at each sample within that line's scan -- the per-pixel
value `p.phocube`'s OMEGA camera model needs to reconstruct cross-track
pointing (see `OMEGA_HK.TXT` and the OMEGA IK's "OMEGA Pixels Geometry"
section). Use **suffix_band=** (1-based) to import one such sideplane
as its own raster instead of the real image bands; output is a single
raster named **output=**. Values are raw (no `OFFSET`/`SCALING_FACTOR`
applied, unlike the regular band import path -- suffix items are
housekeeping integers, not calibrated DN).

Real archives disagree on how wide each band-suffix row is: Cassini
VIMS pads each row to `(samples + suffix_sample_items)` items (per
ISIS3's own `vims2isis` source), but MEX OMEGA's real archived QUBE
uses exactly `samples` items per row. When the label provides
`FILE_RECORDS`/`RECORD_BYTES`/`LABEL_RECORDS` with
`RECORD_TYPE=FIXED_LENGTH`, *p.in.pds3* derives the true per-line byte
stride from those (ground truth) instead of assuming the VIMS
convention, and emits a `G_message` when the two disagree.

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

Import M3's precomputed geometry companion cubes (the same label that
describes the RDN radiance cube also describes these):

```sh
p.in.pds3 -g object=LOC_IMAGE input=M3G20081118T222604_V03_L1B.LBL output=m3_loc
p.in.pds3 -g object=OBS_IMAGE input=M3G20081118T222604_V03_L1B.LBL output=m3_obs
```

Import the real scanning mirror position sideplane from a MEX OMEGA
QUBE (needed by `p.phocube -c instrument=OMEGA_SWIR_C/OMEGA_SWIR_L`):

```sh
p.in.pds3 input=ORB0100_0.QUB output=omega_mirror_dn suffix_band=1
r.univar omega_mirror_dn
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
