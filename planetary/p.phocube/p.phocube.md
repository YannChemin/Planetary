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
3. **Camera mode (`-c`)**: for raw, un-projected pushbroom/framing/
   whiskbroom cubes where `-s` cannot be used at all (no known per-pixel
   (lon, lat) up front). Requires `instrument=` (`CRISM_VNIR`,
   `CRISM_IR`, `ISS_NAC`, `ISS_WAC`, `OMEGA_SWIR_C`, `OMEGA_SWIR_L`,
   `OMEGA_VNIR`, `VIMS_IR`, or `VIMS_VIS`).
   Builds a real per-pixel boresight ray from the instrument's camera
   model (read from the IK/IAK attached via *p.spiceinit*) and
   intersects it with the target surface via `p_spice_sincpt` instead of
   assuming a known surface point. Verified correct against real data
   for all six instruments — see NOTES.

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

#### MEX OMEGA SWIR-C/SWIR-L: a whiskbroom scanning mirror, no IAK needed

`instrument=OMEGA_SWIR_C`/`OMEGA_SWIR_L` add a third, structurally
different camera shape again: not a pinhole focal-plane map at all, but
a real scanning-mirror whiskbroom. Unlike CRISM/ISS, **no IAK exists for
OMEGA** (none on the ISIS3 AWS mirror) -- the whole model comes from the
real public NAIF/ESA IK (`MEX_OMEGA_V03.TI`'s "OMEGA Pixels Geometry"
section): each pixel's pointing is the "central" pixel vector
(boresight, `(0,0,1)` in the detector's own frame) rotated about the
detector frame's `+Y` axis by
`offset_angle = (dn_position - MIRROR_CENTER_POSITION) * MIRROR_SLOPE`
degrees, where `dn_position` is the *real* per-sample scanning-mirror
position (DN) recorded in the cube's own QUBE band-suffix sideplane --
not in the regular image bands, so it must be imported separately via
the new `p.in.pds3 suffix_band=1` option (see that module's docs) and
passed to `-c` as `mirror_dn=`. `MIRROR_CENTER_POSITION`/`MIRROR_SLOPE`
are read from the IK under the shared SWIR id (`INS-41420_*`), not the
per-channel SWIR-C/SWIR-L id, since both InSb arrays share one physical
mirror.

The real FK (`MEX_V16.TF`) centers `MEX_OMEGA_SWIR_C`/`_SWIR_L`'s frame
on the `MEX_OMEGA` instrument body (-41400), which has no SPK ephemeris
of its own (a fixed-mount instrument id, not a tracked body) -- `p.phocube`
works around this by pre-rotating the ray into `MEX_SPACECRAFT` itself
(a one-time, time-independent `pxform`, since both are plain fixed-angle
TKFRAMEs) before calling `sincpt`, since the spacecraft body (-41) does
have real ephemeris throughout.

`instrument=OMEGA_VNIR` is also supported, for the **synced-acquisition**
VNIR product type -- the only kind `p.in.pds3`/`p.in.archive` currently
import (confirmed against a real cube, `ORB0100_0.QUB`:
`CHANNEL_ID=(IRC,IRL,VIS)`, `CORE_ITEMS` sample=64, identical to
SWIR-C/SWIR-L's sample count, not VNIR's native 384/128-pixel pushbroom
width). In this mode VNIR shares SWIR's real per-line/per-sample mirror
telemetry one-for-one -- at each mirror step the same physical sweep
that yields one SWIR sample also yields one VNIR sample -- so the
*identical* `mirror_dn=`/`offset_angle` formula applies, just rotated
out of `MEX_OMEGA_VNIR`'s own detector frame (a fixed `~0.3` deg TKFRAME
offset from `MEX_OMEGA_SWIR_C`, per `MEX_V16.TF`) instead of SWIR's. The
native-resolution, unsynced 128-pixel VNIR pushbroom mode
(`MEX_OMEGA_V03.TI`'s `INS-41410_PIXEL_DN` calibration table) is a
different, currently non-importable product type -- not implemented.

Real-data result, verified against a real MEX OMEGA EDR (orbit 100,
2004-02-10, `ORB0100_0.QUB`) and its own label's known ground-truth
bounds: 100% pixel hit rate, computed lat/lon bounds
(-78.135..-70.253 deg lat, 291.477..302.969 deg E lon) match the
label's own `MINIMUM_LATITUDE`/`MAXIMUM_LATITUDE`/`WESTERNMOST_
LONGITUDE`/`EASTERNMOST_LONGITUDE` (-78.167/-70.253/291.415/303.019) to
within ~0.05 deg -- not just crash-free output. `OMEGA_VNIR` on the same
cube (no independent label ground truth exists for VNIR) also gets a
100% pixel hit rate, with bounds (-78.03..-70.17 deg lat,
291.64..303.00 deg E lon) landing within the expected ~0.3 deg of
SWIR-C's, consistent with the two channels' real, small boresight
offset -- locked in as a regression test
(`test_camera_mode_real_omega_vnir_geometry`).

#### Cassini VIMS_IR/VIMS_VIS: a real 2-axis angular scan, ported from ISIS3

`instrument=VIMS_IR`/`VIMS_VIS` add a fourth camera shape: a genuine
2-axis angular scan, not a focal-plane-mm pinhole at all. Neither the
public NAIF IK (`cas_vims_v06.ti`, which only gives the overall FOV
envelope and a 64x64 nominal pixel grid) nor the IAK (`vimsAddendum04.ti`,
which only fixes a real, documented `CASSINI_VIMS_IR`/`_V` NAIF ID swap
between the public IK and FK -- confirmed from the IAK's own comment)
contains a boresight/pixel-pitch model. The real formula was ported
directly from ISIS3's own `VimsGroundMap::LookDirection()`
(`isis/src/cassini/objs/VimsCamera/VimsGroundMap.cpp`):

```
x = sample + camSampOffset;  y = line + camLineOffset
theta = pi/2 - (y - yBore) * yPixSize
phi   = -pi/2 + (x - xBore) * xPixSize
v = ( sin(theta)*cos(phi), cos(theta), -sin(theta)*sin(phi) )
```

`xPixSize`/`yPixSize`/`xBore`/`yBore` and the integer `camSampOffset`/
`camLineOffset` (note: truncating integer division on purpose, matching
ISIS3's own `int` arithmetic exactly) depend on channel (IR/VIS) and
`SamplingMode` (NORMAL/HI-RES) -- both real per-cube values, not kernel
data, that live only in the PDS3 label's Instrument group
(`SAMPLING_MODE_ID` -- a 2-tuple, `(IR mode, visible mode)` --, plus
`X_OFFSET`/`Z_OFFSET`/`SWATH_WIDTH`/`SWATH_LENGTH`, shared by both
channels). `p.in.archive`'s `vims=`/`opus=` import now writes all of
these into the raster's own `planetary.json` automatically; `-c` reads
them from there, or via the `sampling_mode=`/`x_offset=`/`z_offset=`/
`swath_width=`/`swath_length=` CLI overrides when importing by some
other path. `cam.frame` is the plain NAIF frame (`CASSINI_VIMS_IR`/
`CASSINI_VIMS_V`) -- both have real ephemeris (`FRAME_-8237{0,1}_CENTER
= -82`, the orbiter itself), no `pxform` workaround needed (unlike
OMEGA's `-41400` instrument-body issue).

Real-data result, verified against a real Cassini VIMS cube from the
T-108 Titan flyby (2015-01-08, `v1799424623_1.qub`, IR channel HI-RES,
VIS channel NORMAL, both swaths sharing `X_OFFSET=11 Z_OFFSET=25
SWATH_WIDTH=38 SWATH_LENGTH=18`): both channels land on real,
physically sane, smoothly-varying, overlapping patches of Titan's disk
(IR: lat -65.2..68.1 deg, lon -62.3..104.5 deg; VIS: lat -64.9..74.7 deg,
lon -30.4..92.9 deg -- overlapping but not identical, exactly as
expected for two co-mounted, simultaneously-acquired channels with
different boresight/pixel-pitch/SamplingMode), with real incidence
(9.5-106.7 deg) and emission (7.6-76.4 deg) -- not a degenerate
all-NULL or all-identical output. Both channels' `r.univar` lat/lon
bounds are locked in as a regression test
(`test_camera_mode_real_vims_ir_geometry`/`_vis_geometry`).

### Not implemented (out of scope for this version)

See the repo's top-level `TODO.md` for full context. `-c` supports
CRISM (VNIR and IR detectors), Cassini ISS (NAC and WAC), MEX OMEGA
(SWIR-C, SWIR-L, and synced-acquisition VNIR), and Cassini VIMS (IR and
VIS channels); any other `instrument=` value is a `G_fatal_error`, not
a guess -- other instruments need their own per-instrument camera-model
formula (the pinhole focal-plane convention used for CRISM/ISS is not
necessarily how every instrument's optics work, and neither OMEGA's
whiskbroom nor VIMS's 2-axis angular scan formula generalizes either).
Real (non-ellipsoid) DSK shape models are supported in `-c` the same
way as `-s` (reuses `camera_method`, `"DSK/Unprioritized"` when a DSK
is attached).

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

Camera mode, real per-pixel ray for a raw MEX OMEGA QUBE (real kernels,
verified correct against ORB0100_0.QUB -- see NOTES):

```sh
p.in.pds3 input=ORB0100_0.QUB output=omega_swirc
p.in.pds3 input=ORB0100_0.QUB output=omega_mirror_dn suffix_band=1
g.region raster=omega_swirc.1
p.spiceinit map=omega_swirc.1 target=MARS observer=-41 \
    time=2004-02-10T18:08:35.0475 line_rate=0.401002358 \
    lsk=naif0012.tls sclk=MEX_260522_STEP.TSC \
    ik=MEX_OMEGA_V03.TI fk=MEX_V16.TF \
    pck=MARS_IAU2000_V0.TPC,pck00010.tpc \
    spk=MEX_ROB_040101_041231_003.BSP,de432s.bsp,mar099.bsp \
    ck=ATNM_MEASURED_040101_050101_V03.BC
p.phocube -c -tn instrument=OMEGA_SWIR_C input=omega_swirc.1 \
    output=omega_geom mirror_dn=omega_mirror_dn
# de432s.bsp + mar099.bsp (real NAIF generic planetary ephemeris
# kernels) are needed because the real reconstructed-orbit SPK
# (MEX_ROB_*.BSP) only gives MEX relative to MARS, not all the way to
# the solar system barycenter that sincpt/ilumin need.

# OMEGA_VNIR (synced-acquisition only) reuses the same mirror_dn=
# raster as SWIR-C/SWIR-L above -- only instrument= changes:
p.phocube -c -tn instrument=OMEGA_VNIR input=omega_swirc.1 \
    output=omega_vnir_geom mirror_dn=omega_mirror_dn
```

Camera mode, real per-pixel ray for a raw Cassini VIMS QUBE (real
kernels, verified correct against `v1799424623_1.qub` -- see NOTES):

```sh
p.in.archive vims=v1799424623_1 output=vims_test
# vims= already writes sampling_mode_ir/_vis, x_offset, z_offset,
# swath_width, swath_length into vims_test.1's planetary.json.
p.spiceinit map=vims_test.1 target=TITAN observer=CASSINI \
    time=2015-008T15:09:40.135 \
    lsk=naif0012.tls sclk=cas00172.tsc \
    ik=cas_vims_v06.ti,vimsAddendum04.ti fk=cas_v43.tf \
    pck=cpck_rock_21Jan2011_merged.tpc,pck00010.tpc \
    spk=150108AP_SCPSE_14365_15016.bsp ck=15008_15013ra.bc
p.phocube -c -tn instrument=VIMS_IR input=vims_test.1 output=vims_ir_geom
p.phocube -c -tn instrument=VIMS_VIS input=vims_test.1 output=vims_vis_geom
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
