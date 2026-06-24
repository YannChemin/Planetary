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

Only `ISS_NAC` and `ISS_WAC` are currently supported in camera mode.
CRISM, MEX OMEGA, and Cassini VIMS are **not** supported here: their
pointing is time-varying (CRISM's per-line gimbal CK) and/or
sample-varying (OMEGA's whiskbroom scanning mirror; VIMS's 2-axis
angular scan), which requires a 1-D or 2-D root-search inverse, not the
closed-form algebraic inverse ISS's static single-epoch framing geometry
allows. See `TODO.md` for the status of those instruments.

**Without -c** (legacy mode): a simple flat-field ellipsoid resample --
for each output pixel, computes a local radius from the given ellipsoid
parameters and maps lat/lon directly onto the input raster's own
(`a_radius`/`b_radius`/`c_radius`) region bounds. This mode does **not**
use SPICE or any real camera geometry; it predates the `-c` rebuild and
is kept only for simple non-georeferenced inputs that already carry
plausible lat/lon-like region bounds.

*p.cam2map* does **not** implement named cartographic map projections
(Sinusoidal, Mercator, Lambert Conformal Conic, etc.) -- the output
region's north/south/east/west are always plain lat/lon degrees. Real
cartographic reprojection beyond lat/lon remains a separate, unbuilt
feature (see `TODO.md`).

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
