## DESCRIPTION

*p.phocube* computes per-pixel photometric/geometric backplane rasters
(incidence, emission, phase, latitude, longitude, local radius, pixel
resolution) for a planetary image, against an ellipsoid shape model. For
each output band selected by flags, a separate GRASS raster is written
named **output_bandname**.

Available backplane bands:

| Flag | Band name | Description |
|---|---|---|
| -i | incidence | Solar incidence angle at surface (degrees) |
| -e | emission | Emission angle to sensor (degrees) |
| -p | phase | Phase angle Sun-surface-sensor (degrees) |
| -t | lat | Planetocentric latitude (degrees) |
| -n | lon | Longitude (degrees) |
| -r | local_radius | Local ellipsoid radius (km) |
| -x | resolution | Approximate ground-sample distance (km/pixel) |
| -a | (all of the above) | |

Three operating modes:

1. **Flat-field mode (default)**: the user supplies fixed solar-direction
   (`sun_x/y/z=`) and observer-position (`obs_x/y/z=`) vectors in
   body-fixed coordinates, applied uniformly to every pixel. Each pixel's
   own (lat, lon) is derived directly from the GRASS region's east/north
   — **the active region must already be the scene's real geographic
   footprint** (degrees), not an un-georeferenced sample/line pixel grid
   (see NOTES).
2. **SPICE mode (`-s`)**: real per-pixel ephemeris geometry. Reads
   `target=`/`observer=`/`time=` and kernel paths from the input map's
   history (written by *p.spiceinit*), converts each pixel's region
   east/north to real (lon, lat) — directly if the location is already
   geographic, or via `GPJ_transform()` if it's a projected CRS — builds
   the body-fixed surface point at that (lat, lon) on the body's real
   ellipsoid (radii taken from the loaded PCK, not the `a_radius=`/
   `b_radius=`/`c_radius=` CLI defaults), and calls NAIF CSPICE's
   `ilumin` once per pixel for the real incidence/emission/phase angles
   at the given observation time. Fails with `G_fatal_error` if the
   active region is an un-georeferenced pixel/line grid
   (`PROJECTION_XY`) — see NOTES. If `line_rate=` was also attached via
   *p.spiceinit*, each row gets its own ephemeris time instead of one
   constant epoch for the whole scene (see "Per-line timing" below).
3. **Camera mode (`-c`, v1: CRISM only)**: for raw, un-projected
   pushbroom cubes where `-s` cannot be used at all (no known per-pixel
   (lon, lat) up front). Requires `instrument=` (`CRISM_VNIR` or
   `CRISM_IR`). Builds a real per-pixel boresight ray from the
   instrument's camera model (read from the IK attached via
   *p.spiceinit*) and intersects it with the target surface via
   `p_spice_sincpt` instead of assuming a known surface point.
   **Not yet verified correct against real data — see NOTES. Do not
   trust its output quantitatively yet.**

## NOTES

### Flat-field mode and the region-as-lat/lon assumption

In flat-field mode (no `-s`), `p.phocube` treats the GRASS region's
east/north directly as longitude/latitude with **no projection
awareness** — this is a deliberate, documented simplification, not a
bug, but it means the active region must be set to the scene's real
ground footprint (e.g. from the product's own corner coordinates) before
running it. For raw sensor-grid products (e.g. CRISM TRDR cubes
imported pixel/line via `p.in.pds3 -g`), set a temporary geographic
region matching the real footprint, run `p.phocube`, then copy the
resulting backplanes back onto the cube's native pixel/line region (a
row/col-shaped array round-trip via `r.out.bin`/`r.in.bin`, not a
reprojection, since both regions share the same row/col count) — see
`p.atcorr.md`'s worked Mars example for the full sequence.

### Per-line timing in `-s` mode

By default `-s` uses one mid-scene epoch (`time=`) for every row — an
approximation, since a real pushbroom/framing acquisition takes a real,
non-zero scan duration and each row was actually acquired at a slightly
different time. If a real per-line cadence is known, attach it via
*p.spiceinit*'s `line_rate=` (seconds per output row); `-s` then computes
each row's own ephemeris time as
`time= + (row - (nrows-1)/2) * line_rate` instead of reusing a single
epoch. This still uses one ephemeris time per *row* (not per pixel
within a row) and does not require or imply a real per-pixel camera
model (see "Not implemented" below) — it only refines the timing of an
already-known, already-georeferenced per-pixel (lat, lon).

### Real (DSK) shape models in `-s` mode

By default `-s` still uses the ellipsoid-only shape model
(`a_radius=`/`b_radius=`/`c_radius=`, or real PCK radii), just driven by
real ephemeris instead of fixed flat-field vectors. If a DSK kernel
(real, non-ellipsoid shape model, e.g. from laser altimetry or
stereophotogrammetry) is also attached via *p.spiceinit*'s `dsk=`, `-s`
uses it instead: each pixel's known (lon, lat) is mapped to the real
shape's surface point via CSPICE `latsrf` (`method="DSK/Unprioritized"`)
rather than the ellipsoid intercept, and `ilumin` is then called with
that same method so incidence/emission/phase reflect the real local
surface normal, not the ellipsoid's. This still needs no per-pixel
camera model — `latsrf` only needs the already-known (lon, lat), exactly
like the ellipsoid path it replaces. If `latsrf` has no DSK coverage at
a given pixel's (lon, lat) (e.g. outside the DSK's tiled extent), that
pixel falls back to the ellipsoid rather than failing the whole run.

### SPICE mode (`-s`): scope and requirements

- Requires `target=`/`observer=`/`time=` and at least one kernel to have
  been attached via `p.spiceinit` beforehand. Missing any of these is a
  `G_fatal_error`, not a silent fallback.
- Requires the active region to be a real geographic or projected CRS.
  An un-georeferenced pixel/line grid (`PROJECTION_XY`, as produced by
  `p.in.pds3 -g` for raw, un-projected pushbroom/framing cubes such as
  CRISM TRDR) is rejected with `G_fatal_error` — there is no real camera
  model (instrument boresight/FOV + per-scan-line CK timing) anywhere in
  this suite to compute genuine per-pixel look-direction rays for that
  case, and silently treating sample/line indices as degrees would be
  the wrong answer, not an approximation. Re-project the product to its
  real footprint first (most RDR/COG products, e.g. HiRISE/CTX/MTRDR,
  already are georeferenced and work directly), or use flat-field mode.
- Uses one mid-observation epoch (`time=`) for the whole scene — not
  per-line/per-pixel timing. This is an approximation, adequate for
  framing/pushbroom scenes short enough that spacecraft motion during
  acquisition doesn't materially change the geometry; it is not a real
  per-scanline camera model.
- Still uses the existing ellipsoid-only shape model (no DSK/real-shape
  intercepts) — `-s` changes *which geometry engine drives the
  ellipsoid* (real ephemeris vs. fixed flat-field vectors), not the
  shape model itself.
- A generic planetary ephemeris SPK (e.g. `de430.bsp`, `de440s.bsp`)
  typically carries each *system barycenter* (e.g. MARS BARYCENTER, ID
  4) but not necessarily the individual planet body itself (e.g. MARS,
  ID 499) for bodies whose moons barely perturb the system barycenter.
  `p_spice_ilumin` needs the actual target body's state, so `-s` mode
  requires a kernel set that actually carries that body (e.g. a
  body/mission-specific SPK, as real mission meta-kernels always
  include) — a generic-only planetary ephemeris can produce a CSPICE
  `SPKINSUFFDATA` error for some bodies even though the file loads fine.

### Validated against real data

Confirmed working end-to-end against real NAIF kernels (LSK + PCK +
`de430.bsp`) and a real `p.spiceinit`-attached target/observer/time, in a
real geographic GRASS location: read-history → kernel load → ephemeris
time conversion → real PCK radii → per-pixel `ilumin` all produced sane,
smoothly per-pixel-varying, non-NULL incidence/emission/phase. The
`PROJECTION_XY` guard was confirmed to reject an un-georeferenced
pixel/line input with `G_fatal_error` rather than silently misinterpreting
sample/line indices as degrees. `line_rate=`'s per-row ephemeris time was
confirmed to produce a real, monotonic, row-indexed incidence gradient
(vs. an identical run without `line_rate=`), centered on the mid-scene
row, rather than being silently ignored. DSK shape support was confirmed
against the real PHOBOS shape model (NAIF `phobos_3_3.bds`): `local_radius`
varied realistically (~9-13 km, stddev well above an ellipsoid's smooth
variation over the same patch) instead of the smooth ellipsoid curve, and
a standalone `latsrf` comparison at matched (lon, lat) showed up to ~1.8 km
real divergence from the ellipsoid approximation — Phobos's well-known
large-scale irregularity (e.g. the Stickney crater). All four are covered
by regression tests in `testsuite/test_pphocube.py`.

### Camera mode (`-c`): CRISM and Cassini ISS NAC/WAC, both verified correct

`-c` reuses the same real `sincpt`/`ilumin` calls already verified
correct by `-s` mode (above), driven by a real per-pixel ray instead of
a known surface point. Real-data testing against the actual
FRT00003BFB CRISM observation (this repo's own worked example; real
NAIF MRO kernels, including the CRISM gimbal's own CK/SCLK pairing --
frame `MRO_CRISM_ART`, NAIF ID -74012, decoded via virtual clock ID
-74999, not the regular spacecraft clock) found and fixed one real
crash bug (`p_spice_sincpt`'s `trgepc`/`srfvec` outputs are not
optional -- passing `NULL` segfaults inside CSPICE; now documented in
`p_spice.h`), and confirmed `sincpt`/`ilumin` themselves produce sane,
real, smoothly-varying incidence/emission/phase when driven by the
*unrotated* instrument boresight directly (matching real MRO orbital
altitude and a genuine ~60 deg off-nadir CRISM gimbal angle, consistent
with a real targeted "FRT" observation).

**Now verified correct**, end to end, against the real FRT00003BFB
data: `-c` uses the same focal-plane pinhole convention ISIS3's current
`CrismCamera.cpp` actually uses -- `dx = (sample - BORESIGHT_SAMPLE) *
PIXEL_PITCH`, ray = `(dx, 0, FOCAL_LENGTH)` in the camera frame -- not
the `CAMERA_COEFF` table (which gave a cross-track swing ~6x larger
than the IK's own declared FOV and pushed every ray off-planet; ISIS3
itself never uses `CAMERA_COEFF` for ray geometry either --
`CrismCamera::SetBand()` is a documented no-op). The catch:
`BORESIGHT_SAMPLE`/`BORESIGHT_LINE`/`PIXEL_PITCH`/`FOCAL_LENGTH` are
**not** in the public NAIF IK (`mro_crism_v10.ti`) -- they live in
ISIS3's separately-distributed instrument addendum kernel,
`crismAddendum001.ti`. Fetch it from the ISIS3 AWS data mirror and
attach it via `p.spiceinit`'s `ik=` *in addition to* `mro_crism_v10.ti`:

```sh
curl -O https://asc-isisdata.s3.us-west-2.amazonaws.com/usgs_data/mro/kernels/iak/crismAddendum001.ti
p.spiceinit ... ik=mro_crism_v10.ti,crismAddendum001.ti ...
```

Real-data result on FRT00003BFB (VNIR, 15x64 px): 100% of pixels hit
the planet (0 NULL), `lat`~22.149 deg, `lon`~-17.95 deg (i.e. ~342.05
deg E) -- matching Mawrth Vallis's known location (~22.4N, 341E) almost
exactly; `incidence`~52.6 deg, `emission`~69.7 deg, `phase`~78.8 deg,
all physically sane for a real targeted MRO observation. `band=` was
removed from `-c`'s options -- the real geometry is band-independent
(matching ISIS3), so there was nothing left for it to select.

#### Cassini ISS NAC/WAC: a real 2-D framing camera, not a 1-D slit

`instrument=ISS_NAC`/`ISS_WAC` add a second, structurally different
camera shape: a real 2-D framing camera (both `sample` *and* `line` are
focal-plane offsets, one static boresight per whole frame -- unlike
CRISM, where `line` is time and per-line pointing instead comes from the
gimbal CK) with genuine radial lens distortion (`K1`, ISIS3's own
`RadialDistortionMap` convention: `ux=dx*(1+K1*r2)`, `uy=dy*(1+K1*r2)`,
applied before the ray is built), a custom IAK-defined frame fixing a
real, documented missing 180 deg rotation in NAIF's own `cas_v*.tf`
(`CASSINI_ISS_NAC_USGS`/`_WAC_USGS`, not the bare `CASSINI_ISS_NAC`/
`_WAC`), and a focal length that genuinely varies per filter-wheel pair
(dozens of `INS-8236{0,1}_<F1>_<F2>_FOCAL_LENGTH` IAK keys, not one
constant). `filter1=`/`filter2=` (e.g. `CL1`/`CL2`) select the pair; when
omitted, read from the raster's own `planetary.json` `filter_name` field
(set automatically by `p.in.archive`'s OPUS ISS import); when neither is
available, falls back to the IAK's own `DEFAULT_FOCAL_LENGTH` with a
warning (a real, IAK-documented fallback -- "not being used... but was
left in" -- unlike CRISM's discredited `CAMERA_COEFF` guess).

Both IAKs (`IssNAAddendum005.ti`/`IssWAAddendum005.ti`) come from the
same ISIS3 AWS mirror as CRISM's, fetchable directly via `p.spice.find
spacecraft=CASSINI instrument=ISS_NAC kernels=iak` (or `ISS_WAC`).

Real-data results, both confirmed via OPUS's own `SURFACEGEOsaturn_
rangetobody1` field (Saturn's surface actually in view) before
downloading:

- **NAC** (`co-iss-n1466182140`, 2004-06-17, filter `P0/CB2` -- not in
  the IAK, exercises the `DEFAULT_FOCAL_LENGTH` fallback): 100% pixel
  hit rate, southern-hemisphere latitudes only (-34 to -0.9 deg),
  emission 0.03-34.5 deg, incidence 66.5-92.7 deg -- all physically sane
  for a disk view from ~8.3M km.
- **WAC** (`co-iss-w1466182067`, same sequence, filter `CB2/IRP0` --
  exact IAK match, exercises the non-fallback path): WAC's much wider
  FOV captures the *whole* disk at this range -- ~4% pixel hit rate
  (matching Saturn's small angular size relative to the frame) with full
  -180..180 deg longitude coverage and latitudes from -89.6 to +67.9 deg,
  confirming the whole-disk geometry, not just a crash-free run.

### Not implemented (out of scope for this version)

See the repo's top-level `TODO.md` for full context. `-c` supports
CRISM (VNIR and IR detectors) and Cassini ISS (NAC and WAC); any other
`instrument=` value is a `G_fatal_error`, not a guess -- other
instruments need their own per-instrument camera-model formula (the
pinhole focal-plane convention used here is not necessarily how every
instrument's optics work, e.g. VIMS's whiskbroom scan mirrors -- VIMS's
own IAK exists but only fixes frame-ID housekeeping, no boresight/focal-
length, and MEX/OMEGA has no IAK on the mirror at all; both still need
from-scratch research, not this pinhole convention). Real (non-ellipsoid) DSK
shape models are supported in `-c` the same way as `-s` (reuses
`camera_method`, `"DSK/Unprioritized"` when a DSK is attached).

## EXAMPLES

Flat-field mode, region already set to the scene's real geographic
footprint:

```sh
g.region n=22.406 s=22.272 e=-17.946 w=-18.433 rows=15 cols=64
p.phocube input=crism_mawrth_ir.1 target=mars \
    sun_x=0.55 sun_y=-0.10 sun_z=0.82 \
    obs_x=3254.8 obs_y=-1057.5 obs_z=1406.4 \
    -iep output=mawrth_geom
```

SPICE mode, on an already-georeferenced product (e.g. a HiRISE RDR/COG):

```sh
p.spiceinit map=hirise_red target=MARS observer=MRO \
    time=2007-01-05T01:26:56 \
    lsk=naif0012.tls pck=pck00011.tpc spk=mro_struct.bsp ck=mro_sc.bc
p.phocube -s -iep input=hirise_red output=hirise_geom
# produces: hirise_geom_incidence, hirise_geom_emission, hirise_geom_phase
```

SPICE mode with a real DSK shape model (e.g. an irregular small body)
instead of the ellipsoid approximation, and real per-line timing:

```sh
p.spiceinit map=phobos_img target=PHOBOS observer=MRO \
    time=2026-04-22T14:58:39 line_rate=0.001 \
    lsk=naif0012.tls pck=pck00011.tpc spk=mar097.bsp \
    dsk=phobos_3_3.bds ck=mro_sc.bc
p.phocube -s -iepr input=phobos_img output=phobos_geom
# local_radius (and the surface point feeding incidence/emission/phase)
# now comes from the real shape model, not a smooth ellipsoid.
```

Camera mode, real per-pixel ray for a raw CRISM cube (real kernels,
verified correct against FRT00003BFB -- see NOTES):

```sh
curl -O https://asc-isisdata.s3.us-west-2.amazonaws.com/usgs_data/mro/kernels/iak/crismAddendum001.ti
p.spiceinit map=crism_frt target=MARS observer=MRO \
    time=2007-01-05T01:26:56.855 line_rate=0.266667 \
    lsk=naif0012.tls pck=pck00011.tpc sclk=MRO_SCLKSCET.00119.tsc \
    sclk=MRO_SCLKSCET.00119.65536.tsc fk=mro_v17.tf \
    ik=mro_crism_v10.ti,crismAddendum001.ti \
    spk=mro_psp2.bsp spk=mar063.bsp \
    ck=mro_sc_psp_070102_070108.bc ck=mro_crm_psp_070101_070131.bc
p.phocube -c -iepntr instrument=CRISM_VNIR input=crism_frt output=crism_geom
```

Camera mode, real per-pixel ray for a raw Cassini ISS NAC frame (real
kernels, verified correct against co-iss-n1466182140 -- see NOTES):

```sh
p.spice.find spacecraft=CASSINI instrument=ISS_NAC time=2004-06-17T16:24:48 \
    kernels=lsk,sclk,ik,fk,pck,spk,ck,iak dest=./spice
p.spiceinit map=iss_frame target=SATURN observer=CASSINI \
    time=2004-169T16:24:48.262 \
    lsk=spice/lsk/naif0012.tls pck=spice/pck/cpck_rock_21Jan2011_merged.tpc \
    sclk=spice/sclk/cas00172.tsc fk=spice/fk/cas_v43.tf \
    ik=spice/ik/cas_iss_v10.ti,spice/iak/IssNAAddendum005.ti \
    spk=spice/spk/*.bsp ck=spice/ck/*.bc
p.phocube -c -iepntr instrument=ISS_NAC input=iss_frame output=iss_geom \
    filter1=CL1 filter2=CL2
# filter1=/filter2= optional if the raster's own planetary.json
# 'filter_name' was set by p.in.archive's OPUS ISS import.
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65-70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

- Hapke, B. (1981). Bidirectional reflectance spectroscopy 1. Theory.
  *J. Geophys. Res.* 86(B4):3039-3054.
  doi:[10.1029/JB086iB04p03039](https://doi.org/10.1029/JB086iB04p03039)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.atcorr](p.atcorr.md),
[p.atcorr.hapke](p.atcorr.hapke.md)*

## AUTHOR

Yann Chemin
