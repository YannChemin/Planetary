## DESCRIPTION

*p.cam2map* reprojects a raw planetary camera image into a real lat/lon
grid.

**With -c** (camera mode): for each output pixel, given its real lat/lon,
*p.cam2map* computes the body-fixed surface point (NAIF `latsrf_c`,
Ellipsoid method), the camera-to-surface ray in the spacecraft's camera
frame, and inverts the exact pinhole+K1-radial-distortion projection
used by [p.phocube](p.phocube.md)'s own `-c` forward camera model to
recover the fractional (sample, line) in the raw input image, which is
then bilinearly (or nearest-neighbour) resampled. This is the algebraic
inverse of `p.phocube -c`'s forward ray construction, reusing the exact
same per-instrument camera model (boresight, pixel pitch, focal length,
K1 distortion -- read from the same real ISIS3 instrument kernel/IAK via
`p.spiceinit`'s history). Real aberration-corrected ("LT+S") geometry is
used throughout, matching `p.phocube`'s own forward convention; this
needs care for distant targets where light time is non-negligible
relative to the target's rotation rate (see NOTES).

Supported instruments in camera mode:

- **`ISS_NAC` / `ISS_WAC`** (Cassini): closed-form algebraic inverse -- one
  boresight epoch for the whole frame, real K1 radial distortion corrected by
  5 fixed-point iterations. Per-filter focal length resolved from
  `filter1=`/`filter2=` or the raster's `planetary.json` (written by
  `p.in.archive`'s OPUS import).
- **`CRISM_VNIR` / `CRISM_IR`** (MRO): pushbroom per-line binary search.
  For each output pixel, the scan line whose along-track epoch has the surface
  point exactly cross-track (dvec[1]≈0) is found by bisecting over
  `et(line) = et_mid + (line − mid_line) × line_rate`; ~log₂(nrows) ≈ 9-10
  iterations converge to sub-pixel. The cross-track sample is then a direct
  algebraic inversion (no radial distortion). `line_rate=` must have been
  passed to `p.spiceinit`. Per-pixel SPICE cost: ~30-60 calls (vs 3 for ISS).
- **`OMEGA_SWIR_C` / `OMEGA_SWIR_L` / `OMEGA_VNIR`** (MEX): 2-D whiskbroom
  inverse. Outer binary search over scan lines (same `line_rate=` mechanism as
  CRISM): finds the line where the surface point's along-track component in the
  OMEGA detector frame (`dvec_det[1]`, Y-axis, the axis orthogonal to the
  mirror sweep) crosses zero. Inner sample resolution: the ray from the surface
  point is rotated into the OMEGA detector frame via the precomputed fixed
  `omega_rot` matrix; the mirror elevation angle is extracted algebraically
  (`θ = atan2(dvec_det[0], dvec_det[2])`), converted to a target mirror DN
  (`dn = θ_deg / mirror_slope + mirror_center`), and then a binary search in
  the per-line `mirror_dn=` sideplane raster finds the matching sample
  sub-pixel by interpolation. The `mirror_dn=` raster (written by
  `p.in.pds3 -g` for OMEGA cubes) must be supplied.

Cassini VIMS is **not** yet supported: VIMS needs a 2-axis scan model inverse.
See `TODO.md`.

**Without -c** (legacy mode): a simple flat-field ellipsoid resample --
for each output pixel, computes a local radius from the given ellipsoid
parameters and maps lat/lon directly onto the input raster's own
(`a_radius`/`b_radius`/`c_radius`) region bounds. This mode does **not**
use SPICE or any real camera geometry; it predates the `-c` rebuild and
is kept only for simple non-georeferenced inputs that already carry
plausible lat/lon-like region bounds.

*p.cam2map* supports three output map projections in camera mode, selected
with `projection=`:

- **`latlon`** (default): the output region's north/south/east/west are
  plain lat/lon degrees -- same behaviour as before.
- **`sinusoidal`**: equal-area cylindrical. north/south are still latitude
  degrees; east/west are `(lon − clon) × cos(lat)` degrees. Suitable for
  whole-planet or wide-area maps where area fidelity matters. Set `clon=`
  to the central meridian.
- **`stereo_north`** / **`stereo_south`**: polar stereographic, true-scale
  at the pole, on a sphere. east/west are `sin(lon − clon) × tan(π/4 −
  |lat|/2) × 180/π` degrees and north/south are the corresponding
  perpendicular component. Suitable for polar regions. Set `clon=` to the
  desired central meridian.

For all non-`latlon` projections the output region's coordinates are in
the projection's native units (degrees in the same angular scale as
lat/lon for all three supported projections). `clon=` defaults to 0.
Pixels that inverse-project outside ±90° latitude are set to NODATA.

## NOTES

Camera mode (`-c`) deliberately runs in a `PROJECTION_XY`
(un-georeferenced) GRASS location, with the output region's
north/south/east/west interpreted directly as real lat/lon degrees by
this module's own code -- not validated or interpreted by GRASS's CRS
machinery. This is the same convention every other camera-mode real-data
test in this project already uses (see `p.phocube`'s test suite): a real
`PROJECTION_LL` location hard-enforces +-90 deg latitude at the C library
level, which makes it impossible to also import a raw camera image taller
than 180 rows (a typical raw spacecraft frame, whose native import region
treats row index as a coordinate) into the same location as a real-CRS
output.

The instrument kernel's `BORESIGHT_SAMPLE`/`BORESIGHT_LINE`/
`PIXEL_PITCH` are given for the detector's full (1x1, unbinned)
resolution. Real images acquired in a summed/binned mode (e.g. Cassini
ISS's `INSTRUMENT_MODE_ID=SUM2`, 2x2 binning) need the boresight and
pixel pitch rescaled accordingly; *p.cam2map* detects this automatically
by comparing the IK's own full-frame `PIXEL_SAMPLES`/`PIXEL_LINES` to the
input raster's actual dimensions -- no extra option needed.

For distant targets (e.g. Cassini imaging Saturn from several million
km), one-way light time can be tens of seconds, during which a fast
rotator can turn by a fraction of a degree -- comparable to a narrow-FOV
camera's entire field of view. `-c`'s "LT+S" geometry correctly accounts
for this (the target body's orientation is evaluated at the light-time
corrected epoch, the spacecraft's own camera orientation at the
reception epoch), matching `p.phocube`'s forward `sincpt` convention
exactly.

## EXAMPLES

Back-project a real Cassini ISS NAC frame of Saturn onto a real lat/lon
grid (run in a `PROJECTION_XY` location -- see NOTES):

```sh
p.spiceinit map=iss_nac target=SATURN observer=CASSINI time=2004-169T16:24:48.262 \
    lsk=naif0012.tls sclk=cas00172.tsc ik=cas_iss_v10.ti,IssNAAddendum005.ti \
    fk=cas_v43.tf pck=cpck_rock_21Jan2011_merged.tpc,pck00010.tpc \
    spk=040615AP_SCPSE_04167_04186.bsp ck=04168_04171ra.bc

g.region n=25 s=-44 e=-9 w=-79 res=0.1
p.cam2map -c input=iss_nac output=iss_map instrument=ISS_NAC \
    filter1=P0 filter2=CB2
```

Back-project to a sinusoidal equal-area map (central meridian 0°):

```sh
g.region n=30 s=-30 e=30 w=-30 res=0.1
p.cam2map -c input=iss_nac output=iss_sinusoidal instrument=ISS_NAC \
    filter1=P0 filter2=CB2 projection=sinusoidal clon=0
```

Back-project a CRISM VNIR targeted cube onto a real lat/lon grid
(requires `line_rate=` in `p.spiceinit` -- stored in the SPICE history):

```sh
p.spiceinit map=crism_vnir target=MARS observer=MRO \
    time=2009-182T14:33:05 line_rate=0.03125 \
    lsk=naif0012.tls sclk=MRO_SCLKSCET.00054.65536.tsc \
    ik=crism_v10.ti,crismAddendum001.ti fk=mro_v16.tf \
    pck=pck00010.tpc spk=mro_psp_rec.bsp ck=mro_sc_psp_091221_091227.bc

g.region n=18.6 s=18.1 e=77.9 w=77.4 res=0.001
p.cam2map -c input=crism_vnir output=crism_map instrument=CRISM_VNIR
```

Back-project a MEX OMEGA SWIR_C cube onto a real lat/lon grid
(requires both `line_rate=` in `p.spiceinit` and the `mirror_dn=` sideplane
raster that `p.in.pds3 -g` writes alongside the main spectral cube):

```sh
p.spiceinit map=omega_swir_c target=MARS observer=MEX \
    time=2004-020T12:30:00 line_rate=0.001 \
    lsk=naif0012.tls sclk=MEX_210101_STEP.TSC \
    ik=MEX_OMEGA_V03.TI fk=MEX_V16.TF \
    pck=pck00010.tpc spk=ORMM__080101000000_01795.BSP ck=ATNM_RECONSTITUTED_00003.BC

g.region n=20 s=10 e=50 w=40 res=0.01
p.cam2map -c input=omega_swir_c output=omega_map \
    instrument=OMEGA_SWIR_C mirror_dn=omega_swir_c_mirror_dn
```

Legacy flat-field ellipsoid resample (no SPICE):

```sh
p.cam2map input=raw_image output=resampled \
    a_radius=3396.19 b_radius=3396.19 c_radius=3376.20
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65-70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.phocube](p.phocube.md),
[p.caminfo](p.caminfo.md),
[r.proj](https://grass.osgeo.org/grass-stable/manuals/r.proj.html)*

## AUTHOR

Yann Chemin
