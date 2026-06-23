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
  as the formalized shortcut the priority list below asked for; the OPUS
  API fix from that session still applies (the files API requires the
  `_vis`/`_ir`-suffixed observation id, not the bare one).
  **Raw VIMS `.qub` import unblocked in a later session** -- see the
  `libs/p_pds` QUBE suffix-bytes fix below. `vims=` now also fetches the
  `.fmt` "structure" files (`core_description.fmt`,
  `suffix_description.fmt`, `band_bin_center.fmt`) that real VIMS labels
  reference via `^STRUCTURE` instead of inlining (a second real gap found
  alongside the suffix-bytes one: without these, `p.in.pds3` silently
  fell back to a wrong 8-bit-unsigned default instead of the real
  16-bit-signed DN). Verified live end-to-end against
  `opus.pds-rings.seti.org`: real 352-band Titan flyby cube
  (`v1799424623_1.qub`), sane non-uniform per-band DN ranges (e.g. band 1:
  67-240, band 50: 67-1520), registered as an imagery group like
  crism=/m3=.
- **OMEGA** (`omega=`) -- **done in a later session**, once the QUBE
  suffix-bytes gap below was fixed. Real, live ESA Planetary Science
  Archive source (`archives.esac.esa.int/.../DATA/ORB<NN>/ORB<NNNN>_<N>.QUB`,
  attached label, no companion `.LBL`). Unlike VIMS, OMEGA's
  `CORE_ITEM_BYTES`/`CORE_ITEM_TYPE`/`SUFFIX_ITEM_BYTES` etc. are all
  inlined directly in the QUBE object (no `^STRUCTURE` externalization
  needed). Verified live: real 352-band orbit-100 Mars cube
  (`ORB0100_0.QUB`), sane raw DN within the label's own declared
  saturation bounds (-32768/32767). One real, benign edge case found and
  left as-is (not a bug to fix): the very last image line of real OMEGA
  (and VIMS) cubes runs a few bytes short of a full line for the
  highest-numbered bands -- `p_pds_read_row()` already reports this as a
  per-row read failure, and the existing caller convention
  (`p.in.pds3`'s `write_band()`) already turns that into a GRASS NULL row
  rather than aborting, so it degrades safely (~0.05% of pixels NULL at
  one edge) instead of either crashing or silently misreading.

**Real `libs/p_pds` gaps found and fixed (across two sessions)**: real
PDS3 QUBE products from multiple archives (OMEGA, VIMS, and M3's
detached-label convention) didn't fit the reader's original assumptions.
Fixes landed:
1. A generic `*_IMAGE`/`*_QUBE` object-name fallback (M3's label uses
   `OBJECT = RDN_IMAGE`/`^RDN_IMAGE`, not the standard
   `IMAGE`/`QUBE`/`SPECTRAL_QUBE` name) -- this is what unblocked M3.
2. Parsing the tuple-valued `CORE_ITEMS`/`SUFFIX_ITEMS` keywords (e.g.
   `CORE_ITEMS = (64,352,672)`, ordered per `AXIS_NAME`), as a fallback
   alongside the older `LINES`/`LINE_SAMPLES`/`BANDS` and
   `CORE_ITEMS_1`/`_2`/`_3` keyword conventions.
3. **QUBE sample-/band-suffix (sideplane/backplane) byte skipping**, for
   `BAND_STORAGE_TYPE = LINE_INTERLEAVED` (BIL) cubes with a zero
   line-suffix -- the real layout both OMEGA (`SUFFIX_ITEMS = (1,7,0)`)
   and VIMS (`SUFFIX_ITEMS = (1,4,0)`) actually use, verified against
   NASA's own ISIS3 `ReadVimsBIL()` (`isis/src/cassini/apps/vims2isis/main.cpp`)
   as the authoritative reference for the byte layout, then verified
   directly against real downloaded `.qub` files (sane, non-uniform,
   correctly-ranged DN values; cross-checked against a parallel manual
   `p.in.pds3` run). `p_pds_open_image_named()` now also infers BIL
   from `AXIS_NAME = (SAMPLE,BAND,LINE)` alone when `BAND_STORAGE_TYPE`
   is absent (true for both real archives) , and tolerates the
   object-name/pointer-keyword mismatch real VIMS labels have
   (`OBJECT = SPECTRAL_QUBE` but `^QUBE = (...)`, not `^SPECTRAL_QUBE`).
   Any other organisation or a nonzero line-suffix is still refused
   (`G_warning` + clean failure) rather than guessed.
4. **`^STRUCTURE` external "structure file" support** -- some QUBE
   archives (confirmed: VIMS) factor `CORE_ITEM_BYTES`/`CORE_ITEM_TYPE`/
   `CORE_NULL`/etc. out of the main label into small shared `.fmt` files
   referenced via `^STRUCTURE = "core_description.fmt"` rather than
   inlining them (OMEGA inlines everything directly, no `^STRUCTURE`
   needed). Found live: without this, `p.in.pds3` silently fell back to
   a wrong 8-bit-unsigned default instead of the real 16-bit-signed DN
   -- no error, no warning, just wrong data. `p.in.archive`'s `vims=`
   path now also fetches the `.fmt` files OPUS already enumerates
   alongside the `.qub`/`.lbl`.

Also fixed, found via live OMEGA/VIMS end-to-end testing (not specific to
the QUBE suffix work, but only surfaced by it): `crism=`, `omega=`, and
the OPUS `vims=`/ISS path were all silently failing to apply their
detector-specific `sensor=`/`mission=` metadata, because `p.in.pds3`
already writes a generic `planetary.json` for every map it creates (from
the label's own `INSTRUMENT_ID`/`MISSION_NAME`), and
`p_meta.write_planetary_metadata()` is deliberately create-only
(first-write-wins) -- so the more specific follow-up call was always a
silent no-op. Fixed by loading the existing record and updating it in
place instead of calling the create-only helper, in all four affected
call sites.

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
- **Resolved**: the real `CAMERA_COEFF`-based rotation (a0=-1.146 rad,
  a1=0.00352 rad/sample for VNIR reference band 223) pushed every
  sample's ray off the planet (0/960 pixels hit) -- a ~13 deg swing
  across 640 samples, far larger than the IK's own declared FOV envelope
  (`FOV_CROSS_ANGLE=0.0185 rad ~= 1.06 deg` half-angle). ISIS3's actual
  current `CrismCamera.cpp` doesn't use `CAMERA_COEFF` at all -- it
  builds the focal-plane mapping from `INS-74017_BORESIGHT_LINE`/
  `_SAMPLE` + a generic pinhole model instead. Those keywords (plus
  `PIXEL_PITCH`/`FOCAL_LENGTH`) aren't in the public NAIF IK
  `mro_crism_v10.ti` -- they live in ISIS3's separately-distributed
  instrument addendum kernel. Found it by reading `isis/scripts/
  downloadIsisData` and `isis/config/rclone.conf` in the ISIS3 source
  tree: the `mro_usgs` rclone remote is a public AWS S3 bucket
  (`asc-isisdata`), browsable over plain HTTPS without any AWS
  credentials (`https://asc-isisdata.s3.us-west-2.amazonaws.com/
  ?list-type=2&prefix=usgs_data/mro/kernels/iak/`), which lists
  `crismAddendum001.ti` directly:
  `https://asc-isisdata.s3.us-west-2.amazonaws.com/usgs_data/mro/
  kernels/iak/crismAddendum001.ti`. Real values for both detectors:
  `PIXEL_PITCH=0.027` mm, `FOCAL_LENGTH=441.0` mm,
  `BORESIGHT_SAMPLE=320.0`, `BORESIGHT_LINE=0.0`. Switched `p.phocube`'s
  `load_crism_camera_model()`/ray-construction to the real pinhole
  formula (`dx = (sample - boresight_sample) * pixel_pitch`, ray =
  `(dx, 0, focal_length)`), dropped the now-unused `CAMERA_COEFF`
  lookup, `band=` option, and `rodrigues_rotate()` helper entirely
  (ISIS3's own `CrismCamera::SetBand()` is a documented no-op -- the
  real geometry is band-independent). Verified end-to-end against
  FRT00003BFB with `crismAddendum001.ti` attached via `p.spiceinit`'s
  `ik=`: 100% of pixels hit the planet (0/960 NULL), computed lat/lon
  (~22.149N, ~342.05E) matches Mawrth Vallis's known location (~22.4N,
  341E) almost exactly, and incidence/emission/phase (~52.6/69.7/78.8
  deg) are physically sane for this real targeted MRO observation.

`-c` is now verified correct against real data, not just crash-free.
Flagged as such in `p.phocube.md`.

### ISIS3 AWS data mirror (asc-isisdata) -- fully indexed; unblocks plan items 2 and 4

Indexed the bucket's full `usgs_data/<mission>/kernels/iak/` listing for
all 35 missions it carries (apollo15/16/17, base, cassini,
chandrayaan1/2, clementine1, dawn, galileo, hayabusa/2, juno, kaguya,
kplo, legacy_base, lo, lro, mariner10, mer, messenger, mex, mgs, mro,
msl, near, newhorizons, odyssey, osirisrex, rolo, rosetta, smart1, tgo,
viking1/2, voyager1/2). Notable findings for this repo's own
instruments/roadmap:

- **Cassini ISS NAC/WAC** (`IssNAAddendum005.ti`/`IssWAAddendum005.ti`):
  real `BORESIGHT_LINE`/`BORESIGHT_SAMPLE` + per-filter
  `*_FOCAL_LENGTH` values -- a genuine, ready-to-use pinhole camera
  model, confirming plan item 2 (Cassini ISS framing camera) is the
  easiest remaining instrument, same pinhole convention as CRISM.
- **Cassini VIMS** (`vimsAddendum04.ti`): exists, but only fixes
  `CK_FRAME_ID`/`NAIF_BODY_CODE` housekeeping (a real, documented
  discrepancy in the public VIMS IK's frame ID assignment) -- **no**
  `BORESIGHT`/`FOCAL_LENGTH` values. Confirms plan item 4 (VIMS) still
  needs its own from-scratch 2-D scan-mirror research; this mirror
  doesn't shortcut it.
- **MEX/OMEGA**: no IAK at all on this mirror (`mex/kernels/iak/` only
  has `hrscAddendum*`/`hrscsrcAddendum*`, for HRSC, not OMEGA).
  Confirms plan item 3 (OMEGA) needs its own from-scratch research too,
  not a borrowed ISIS3 convention.
- Many other instruments not yet in `p.in.archive`/`p.phocube` also have
  ready IAKs here if/when added: HiRISE/CTX/MARCI (MRO), MOC (MGS),
  THEMIS (Odyssey), LRO NAC (`lro_instrumentAddendum_v05.ti`), MDIS
  (MESSENGER), Dawn FC/VIR, New Horizons LORRI/MVIC/LEISA, OSIRIS-REx
  OCAMS/TAGCAMS, Rosetta OSIRIS NAC/WAC/VIRTIS, TGO CaSSIS, Kaguya
  TC/MI, Hayabusa/2 AMICA/NIRS/ONC, Galileo SSI, Clementine, Apollo
  Metric/Pan, Viking, Voyager, Mariner10, Lunar Orbiter.

Wired this into `p.spice.find`: new `kernels=...,iak` fetch type (S3
REST `list-type=2` XML listing, no AWS credentials needed over plain
HTTPS), `AWS_MISSION_DIR` mapping (NAIF dir -> bucket mission slug;
currently MRO/CASSINI/LRO/MESSENGER -- not every NAIF mission has a
slug here, e.g. VEX/Venus Express does not), and `iak_prefix` entries in
`INSTRUMENT` for `CRISM`, `ISS_NAC`, `ISS_WA`, `VIMS`. Verified live:
`-l` lists the correct latest file for all four, and a real download of
`crismAddendum001.ti` round-trips correctly. Added
`TestNetworkIak` to `p.spice.find`'s test suite (5 tests, all passing
live) confirming presence/absence per the findings above.

**Known gap, not yet fixed**: this machine has a system-wide addon
install at `/usr/lib/grass/addons/scripts/p.spice.find` (root-owned,
ahead of `$GRASS_ADDON_BASE=~/.grass8/addons` on `PATH`) that is now
stale relative to the source tree and `/usr/local/grass86/scripts/`
(the `make install` target used during this session). Needs `sudo cp
/usr/local/grass86/scripts/p.spice.find /usr/lib/grass/addons/scripts/p.spice.find`
(or removing/fixing that system install so `PATH` resolves the
`GRASS_ADDON_BASE` copy instead) to pick up this change for normal
`p.spice.find` invocations outside this dev session.
