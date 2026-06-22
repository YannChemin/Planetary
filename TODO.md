# TODO

Tracked items explicitly deferred out of scope while building `p.phocube`'s
real SPICE mode (`-s`). See `planetary/p.phocube/p.phocube.md` NOTES and
`planetary/p.spiceinit/p.spiceinit.md` for the current state of that work.

## 0. Hyperspectral / UV-VIS instrument coverage in `p.in.archive`

(Module renamed from `p.in.astropedia` to `p.in.archive` this session --
it fetches from several real remote archives, not just USGS Astropedia.)

`p.in.archive.py` supports real, non-STAC retrieval paths beyond the
generic Astropedia STAC search, each following the same pattern: a
curated `<NAME>_CATALOG` dict of verified-live URLs (or an API query
builder), a `resolve_<name>()` function, a `print_<name>_catalog()` `-l`
listing, and a dedicated `<name>=` option:

- **CRISM** (`crism=`) -- MRO/CRISM Targeted RDR, fetched directly from
  the PDS Geosciences Node's static archive tree (`pds-geosciences.wustl.edu`),
  since CRISM is indexed neither by OPUS (outer-planet/ring-science only)
  nor the NASA PDS Federated Search. Currently one observation catalogued
  (`FRT00003BFB`, Mawrth Vallis -- this repo's own Mars Mineralogy chapter).
- **M3** (`m3=`) -- **done this session.** Chandrayaan-1 Moon Mineralogy
  Mapper L1B radiance, fetched from the JPL PDS Imaging Node
  (`planetarydata.jpl.nasa.gov`). Verified end-to-end: real product
  imported as an 85-band imagery group, non-degenerate per-band radiance
  confirmed via `r.univar`. Required a real `libs/p_pds` fix (below).
  **Geometry companions added in a later session** (`-g` flag): M3's
  L1B label also describes LOC_IMAGE (per-pixel longitude/latitude/
  radius, 3 bands) and OBS_IMAGE (illumination/viewing geometry, 10
  bands) side by side with RDN_IMAGE, all pointing at separate
  `*_LOC.IMG`/`*_OBS.IMG` files living next to the radiance cube in the
  same archive directory. Unlike CRISM, M3 ships this geometry
  precomputed -- no SPICE/camera-model step needed, just fetching and
  importing the extra cubes. This needed a real `libs/p_pds` API
  addition (`p_pds_open_image_named()`, exposed as `p.in.pds3 object=`)
  since a label describing several image objects side by side previously
  always returned the first one found. Verified live: real longitude/
  latitude/radius/phase-angle values for the same FRT-adjacent M3 orbit,
  sane (radius ~1736-1738 km, matching the Moon).
- **Cassini ISS/VIMS** (`opus=`/`opus_id=`, and now `vims=`) -- via the
  PDS Ring-Moon Systems Node's OPUS API, sensor inferred from the OPUS
  product ID prefix (`co-iss-n*`/`co-iss-w*`/`co-vims-*`). `vims=` added
  this session as the formalized shortcut the priority list below asked
  for, but raw VIMS `.qub` import itself remains blocked (see below) --
  `vims=` correctly fetches the right file (and fixed a real OPUS-API
  bug along the way: the files API requires the `_vis`/`_ir`-suffixed
  observation id, not the bare one) but `p.in.pds3` now explicitly
  refuses to read the cube rather than silently misread it.
- **OMEGA** -- **investigated, not added.** Real, live ESA Planetary
  Science Archive source confirmed
  (`archives.esac.esa.int/psa/ftp/MARS-EXPRESS/OMEGA/MEX-M-OMEGA-2-EDR-FLIGHT-V1.0/DATA/ORB<NN>/ORB<NNNN>_<N>.QUB`,
  attached-label PDS3 QUBE, `TARGET_NAME = MARS` confirmed live), but
  blocked on the same real gap that blocked VIMS (below) -- no `omega=`
  option added since it would have nothing to actually import yet.

**Real `libs/p_pds` gap found and partially fixed this session**: real
PDS3 QUBE products from multiple archives (OMEGA, VIMS, and M3's
detached-label convention) don't fit the reader's original assumptions.
Two fixes landed:
1. A generic `*_IMAGE`/`*_QUBE` object-name fallback (M3's label uses
   `OBJECT = RDN_IMAGE`/`^RDN_IMAGE`, not the standard
   `IMAGE`/`QUBE`/`SPECTRAL_QUBE` name) -- this is what unblocked M3.
2. Parsing the tuple-valued `CORE_ITEMS` keyword (e.g.
   `CORE_ITEMS = (64,352,672)`, ordered per `AXIS_NAME`), as a fallback
   alongside the older `LINES`/`LINE_SAMPLES`/`BANDS` and
   `CORE_ITEMS_1`/`_2`/`_3` keyword conventions.

What's still blocked, for both **OMEGA** and **VIMS**: their QUBE
objects carry non-zero `SUFFIX_ITEMS` (real per-record
sideplane/backplane bytes -- OMEGA `(1,7,0)`, VIMS `(1,4,0)`), which
`libs/p_pds` does not yet know how to skip. Tried to work out the exact
byte layout from ISIS3's own real, production VIMS importer
(`ReadVimsBIL()` in `isis/src/cassini/apps/vims2isis/main.cpp`) -- even
ISIS3 needs a dedicated, hand-written importer for this, not a generic
one, and the byte accounting still didn't fully reconcile against
OMEGA's real file size when worked through by hand. Rather than ship a
half-verified offset formula that would silently misread real science
data (confirmed by trial: reading on as if `SUFFIX_ITEMS` were zero
doesn't crash, doesn't obviously fail, just shifts every subsequent
record and produces wrong-but-plausible pixel values for hundreds of
bands), `libs/p_pds` now explicitly refuses to open any QUBE object with
non-zero `SUFFIX_ITEMS` (`G_warning` + clean failure, confirmed via a
real test against both real labels). Implementing the real per-axis
suffix-skip correctly is real, separate, dedicated work for a future
session -- the safety guard means no one can accidentally ship corrupted
imports in the meantime.

The live Astropedia STAC catalog itself (`stac.astrogeology.usgs.gov`)
currently has only 14 collections, none of them hyperspectral/UV-VIS
spectrometer products (LOLA, Galileo/Voyager controlled mosaics, Kaguya
TC, MRO HiRISE/CTX/THEMIS-IR) -- so any further hyperspectral/UV-VIS
instrument needs the same kind of dedicated, verified-archive-path
support CRISM/M3 got, not a STAC search. Remaining real candidate
instruments, by body, with their real archive location (so the next
implementer doesn't have to re-discover this):

| Body / region | Instrument | Mission | Type | Real archive |
|---|---|---|---|---|
| Mars | OMEGA | Mars Express | VIS+NIR imaging spectrometer, 0.38-5.1 um | ESA Planetary Science Archive (PSA) |
| Mars | MAVEN/IUVS | MAVEN | UV spectrograph (upper atmosphere) | NASA PDS Atmospheres Node |
| Moon | M3 (Moon Mineralogy Mapper) | Chandrayaan-1 | VNIR imaging spectrometer, 0.43-3.0 um | NASA PDS Imaging Node (also via Astropedia's own M3 mosaic in some contexts -- check STAC first) |
| Moon | LAMP | LRO | Far-UV spectrograph (airglow/exosphere) | NASA PDS Atmospheres Node |
| Venus | VIRTIS | Venus Express | Imaging spectrometer, 0.25-5.1 um | ESA PSA |
| Venus | IR1/IR2/UVI | Akatsuki | Multispectral imagers (not hyperspectral, but UV-VIS-NIR) | JAXA DARTS |
| Mercury | MASCS (UVVS+VIRS) | MESSENGER | UV-VIS-NIR point spectrometer | NASA PDS Geosciences Node |
| Mercury | MERTIS | BepiColombo (en route/ongoing) | Thermal-IR imaging spectrometer | not yet archived -- check ESA PSA as mission progresses |
| Saturn system | UVIS | Cassini | UV spectrograph (rings, atmospheres) | PDS Ring-Moon Systems Node (OPUS) -- same access path as VIMS/ISS, just needs its own sensor-prefix mapping |
| Saturn system | VIMS | Cassini | Imaging spectrometer, 0.35-5.1 um | already partially supported via `opus=` (sensor inference only; no dedicated catalog/shortcut yet) |
| Jupiter system | NIMS | Galileo | Near-IR imaging spectrometer | NASA PDS Imaging Node |
| Pluto/Charon | LEISA (on Ralph) | New Horizons | IR imaging spectrometer | NASA PDS Small Bodies Node (SBN) |
| Vesta/Ceres | VIR | Dawn | VIS+IR imaging spectrometer | NASA PDS Small Bodies Node (SBN) |
| 67P/C-G | VIRTIS | Rosetta | Imaging spectrometer | ESA PSA |
| Ryugu | NIRS3 | Hayabusa2 | NIR point spectrometer | JAXA DARTS |
| Bennu | OVIRS | OSIRIS-REx | VIS-NIR point spectrometer | NASA PDS Small Bodies Node (SBN) |
| Europa (future) | MISE | Europa Clipper | Imaging spectrometer | not yet archived (mission en route) |
| Jupiter system (future) | MAJIS | JUICE | Imaging spectrometer | not yet archived (mission en route) |

Status of the original priority list (OMEGA, M3, VIMS): **M3 done**
(real, verified, working import). **VIMS partially done** -- the
`vims=` shortcut and a real OPUS-API bug are fixed, but raw `.qub`
import itself is blocked on the `SUFFIX_ITEMS` gap above. **OMEGA
investigated, blocked on the same gap**, no CLI option added yet.
Next real step for either: implement the per-axis `SUFFIX_ITEMS`
byte-skip in `libs/p_pds` correctly enough to trust on real data (the
ISIS3 `ReadVimsBIL()` reference above is the right starting point, but
its byte accounting needs to be re-verified against a real file size
before trusting it, per this session's experience). Point spectrometers
(MASCS, NIRS3, OVIRS) are a different shape of product (single spectra,
not imaging cubes) and may not fit `p.in.archive`'s current
per-pixel-cube import model without changes -- worth a separate scoping
pass before starting one.

## 1. Per-line/per-pixel timing in `p.phocube -s`

`-s` currently uses one mid-scene epoch (`time=`, attached via
`p.spiceinit`) for the whole image. Real pushbroom/framing acquisitions
(HiRISE, CTX, CRISM, ...) take place over a real, non-zero scan duration,
so each row was actually acquired at a slightly different time. Add an
optional per-line cadence so `-s` can compute a real per-row ephemeris
time instead of one constant epoch for every row, without yet requiring
a full per-pixel camera model (item 3 below).

Status: **done**. `p.spiceinit` gained `line_rate=` (seconds/row),
stored as `SPICE_LINE_RATE=`; `p.phocube -s` computes
`et_row = time + (row - (nrows-1)/2) * line_rate` per row when present
(falls back to the single mid-scene epoch when absent — no behaviour
change for existing scenes). Verified with real LSK/PCK/SPK kernels: a
`line_rate=0.5` run vs. an identical run without it produces a real,
monotonic, row-indexed incidence gradient centered on the mid-scene row
(not noise, not zero). Regression test:
`test_spice_mode_line_rate_produces_row_gradient` in
`planetary/p.phocube/testsuite/test_pphocube.py`.

## 2. Real (non-ellipsoid) shape models / DSK intercepts in `-s` mode

`-s` v1 keeps the existing ellipsoid-only math (`p_shape_latlon_to_xyz` /
`p_shape_local_radius_km`), just driving it with real ephemeris instead
of fixed flat-field vectors. Extend `-s` to use a real DSK shape model
(`p_spice_sincpt`) when a DSK kernel has been attached via
`p.spiceinit`, falling back to the ellipsoid when none is present.

Status: **done**. Added `p_spice_latsrf()` (wraps CSPICE `latsrf_c`) to
`libs/p_spice` -- maps a known (lon, lat) directly to a real surface
point with no observer/look-direction ray needed (unlike `sincpt`),
exactly matching `-s` mode's existing "pixel already has a known
(lon, lat)" architecture. `p.spiceinit` gained `dsk=`, stored as
`SPICE_DSK=`; `p.phocube -s` calls `latsrf` with
`method="DSK/Unprioritized"` per pixel when a DSK was loaded (falls back
to the ellipsoid for any (lon, lat) outside the DSK's coverage, rather
than failing the whole run), and then calls `ilumin` with the same
method so incidence/emission/phase reflect the real local surface
normal. Verified two ways with the real PHOBOS shape model
(`phobos_3_3.bds`, NAIF generic_kernels): (1) a standalone
ellipsoid-vs-DSK `latsrf` comparison at matched (lon, lat) showed up to
~1.8 km real divergence (Phobos's well-known irregularity, e.g. the
Stickney crater); (2) `p.phocube -s -r` produced a real, non-degenerate
`local_radius` (~9-13 km, stddev far above what a smooth ellipsoid would
give over the same patch). Full incidence/emission/phase verification on
PHOBOS itself was blocked only by the lack of a small enough real
Phobos-ephemeris SPK on this machine (`mar097.bsp`/`mar099.bsp` are
>1GB) -- a kernel-availability limitation, not a code gap; the
ellipsoid-mode ilumin path is already separately verified (see
"Per-line timing" above). Regression test:
`test_spice_mode_dsk_shape_differs_from_ellipsoid` in
`planetary/p.phocube/testsuite/test_pphocube.py`.

## 3. Real per-pixel camera-model back-projection (CRISM TRDR, raw EDR)

No module in this repo (`p.cam2map`, `p.caminfo` included) does real
per-pixel camera-model back-projection: instrument-kernel boresight/FOV +
per-scan-line CK orientation + SCLK timing -> a real look-direction ray
per raw sensor pixel. This is what raw, un-projected pushbroom/framing
cubes (CRISM TRDR, raw EDR) actually need for `-s`-equivalent real
geometry, since they have no usable region CRS at all (`p.phocube -s`
currently fails loudly on these with `G_fatal_error` rather than guess).
Materially larger than items 1-2 and likely instrument-by-instrument.
Needs its own plan: decide whether to extend the existing (currently
flat-field-only) `p.cam2map`/`p.caminfo`, or add a new
instrument-specific module (e.g. starting with CRISM).

Status: **implemented, partially verified -- one real conventions bug
still open**. Decision: extended `p.phocube` (new `-c` flag), not
`p.cam2map` -- research this session found `p.cam2map`'s actual code is
pure ellipsoid flat-field resampling despite its docs claiming SPICE
support (same doc/implementation mismatch `-s` mode fixed earlier), and
`p.phocube` already has the right per-input-pixel backplane shape plus
(from items 1-2) the kernel-history/line_rate/DSK machinery to reuse.
`-c` requires `instrument=` (v1: `CRISM_VNIR`/`CRISM_IR` only -- a
distinct per-instrument undertaking for anything else, as expected) and
`band=` (defaults to the IK's own reference band). Added
`p_spice_gdpool_d()` (generic kernel-pool array reader, wraps `gdpool_c`)
to `libs/p_spice`. Per pixel: cross-track angle from CRISM's own
documented `INS-74017_CAMERA_COEFF` table
(`angle = a0(band) + a1(band)*sample`, read directly from the real NAIF
IK `mro_crism_v10.ti`), Rodrigues-rotate the boresight by that angle
about the real `SLIT_DIRECTION` axis, then reuse `p_spice_sincpt()` +
`p_spice_ilumin()` (same calls `-s` mode already uses for the
ellipsoid/DSK case).

Real-data attempt (FRT00003BFB, 2007-01-05, this repo's own worked
example) found and fixed one genuine code bug, confirmed the math, and
surfaced one real open question:

- **Found and fixed a real crash bug**: `p_spice_sincpt()`'s `trgepc`/
  `srfvec` output parameters are NOT optional -- CSPICE writes through
  them unconditionally, so passing `NULL` (as the first `-c` draft did)
  segfaults deep inside `sincpt_c`'s internal `zzsfxcor_`/`vsub_`. Fixed
  by passing real local buffers; documented the requirement in
  `p_spice.h` so the next caller doesn't repeat it.
- **Rodrigues rotation verified correct** standalone (theta=0 reproduces
  the boresight exactly, theta=pi/2 matches the right-hand-rule
  expectation, norm stays 1, the sweep is monotonic).
- **Real CK/SCLK pairing for CRISM's gimbal resolved**: CRISM's
  cross-track pointing is a separate articulation frame
  (`MRO_CRISM_ART`, NAIF ID -74012) driven by its own CK
  (`mro_crm_psp_*.bc`, distinct from the regular spacecraft-body CK) and
  decoded via a *different* virtual clock ID, -74999 (confirmed from
  ISIS3's own `CrismCamera.cpp`: `getClockTime(sclk, -74999)`), requiring
  the `.65536`-suffixed MRO SCLK variant, not the plain one. Both are
  real, archived, fetchable NAIF kernels.
- **Confirmed `sincpt`/`ilumin` work correctly end-to-end on real
  ephemeris** for this exact observation when the *unrotated* boresight
  is used directly (no `CAMERA_COEFF` rotation applied): real MRO
  altitude (|r|=3670 km, matching ~270 km over a 3396 km body), a
  genuine ~60 deg off-nadir gimbal angle (expected for CRISM's targeted
  "FRT" mode, which deliberately gimbals off the ground track), and
  sane, smoothly row-varying incidence/emission/phase (~52/70/80 deg)
  using the real `START_TIME=2007-01-05T01:26:56.855` and real
  `FRAME_RATE=3.75 Hz` (`line_rate=1/3.75`) read directly from the
  product's own PDS label.
- **Open issue**: applying the real `CAMERA_COEFF` per-sample rotation
  (a0=-1.146 rad, a1=0.00352 rad/sample for VNIR reference band 223) on
  top of that already-correct pointing pushes every sample's ray off the
  planet (0/960 pixels hit). The magnitude is the red flag:
  `CAMERA_COEFF`'s ~13 deg swing across 640 samples is far larger than
  the IK's own declared FOV envelope
  (`FOV_CROSS_ANGLE=0.0185 rad ~= 1.06 deg` half-angle), so either the
  per-sample angle isn't meant to be applied as a simple rotation of the
  raw `BORESIGHT` vector the way this v1 does, or there's a missing
  reference subtraction/offset. ISIS3's actual current `CrismCamera.cpp`
  doesn't use `CAMERA_COEFF` at all -- it builds the focal-plane mapping
  from `INS-74017_BORESIGHT_LINE`/`_SAMPLE` + a generic
  `CameraFocalPlaneMap`/pinhole model instead (those keywords aren't in
  the public NAIF IK `mro_crism_v10.ti`; ISIS3 must source them from its
  own separately-distributed data area, not its git source tree).
  **Next step**: switch the per-sample angle formula to the simpler
  pinhole convention (`angle = atan((sample - boresight_sample) *
  pixel_pitch / focal_length)`) matching ISIS3's real, current,
  production CRISM camera model, instead of the legacy `CAMERA_COEFF`
  table this v1 used.

Until that's resolved, `-c` is real, crash-free, and exercises the right
kernels/frames/timing, but its per-sample cross-track angle is not yet
verified correct against real data -- do not trust its output
quantitatively yet. Flagged explicitly in `p.phocube.md`.
