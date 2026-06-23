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

Status of the original priority list (OMEGA, M3, VIMS): all **three now
done** -- real, verified, working raw `.qub`/`.QUB` import for all three
(M3, VIMS, and OMEGA), once the `SUFFIX_ITEMS` byte-skip gap above was
fixed (and, for OMEGA specifically, a second real `libs/p_pds` gap found
later -- the band-suffix row-width mismatch between VIMS's and OMEGA's
real archives, see "MEX OMEGA SWIR-C/SWIR-L camera model" below).
OMEGA additionally now has a working `p.phocube -c` camera model
(SWIR-C/SWIR-L; VNIR still deferred) -- see that section. Point
spectrometers (MASCS, NIRS3, OVIRS) are a different shape of product
(single spectra, not imaging cubes) and may not fit `p.in.archive`'s
current per-pixel-cube import model without changes -- worth a separate
scoping pass before starting one.

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

Status: **done, including the full incidence/emission/phase
verification originally blocked**. Added `p_spice_latsrf()` (wraps
CSPICE `latsrf_c`) to `libs/p_spice` -- maps a known (lon, lat) directly
to a real surface point with no observer/look-direction ray needed
(unlike `sincpt`), exactly matching `-s` mode's existing "pixel already
has a known (lon, lat)" architecture. `p.spiceinit` gained `dsk=`,
stored as `SPICE_DSK=`; `p.phocube -s` calls `latsrf` with
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
give over the same patch). Regression test:
`test_spice_mode_dsk_shape_differs_from_ellipsoid` in
`planetary/p.phocube/testsuite/test_pphocube.py`.

Full incidence/emission/phase verification on PHOBOS itself was
originally blocked only by the lack of a small enough real
Phobos-ephemeris SPK on this machine (`mar097.bsp`/`mar099.bsp` are
>1GB) -- a kernel-availability limitation, not a code gap. **Unblocked
incidentally**: `mar099.bsp` (1.2GB) and `de432s.bsp` (10MB, generic
planetary ephemeris) were fetched in a later session for the MEX OMEGA
camera-model work (see below), since OMEGA's own real reconstructed-
orbit SPK only gives MEX relative to MARS, not all the way to the solar
system barycenter -- the same chain PHOBOS's real ephemeris needs.
Verified end-to-end: `p.spiceinit target=PHOBOS observer=EARTH
spk=de432s.bsp,mar099.bsp dsk=phobos_3_3.bds` + `p.phocube -s -iepr`
produces a 100% pixel hit rate and physically sane real
incidence/emission/phase (58.6-138.6/74.6-152.3/16.164 deg over a 30x30
deg patch -- phase angle barely varies at Earth-Mars range, as expected,
not a degenerate constant) on the real irregular DSK shape, not the
ellipsoid. Regression test:
`test_spice_mode_dsk_real_incidence_emission_phase` in
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

Status: **implemented and verified correct against real data for six
instruments** (`CRISM_VNIR`/`CRISM_IR`, `ISS_NAC`/`ISS_WAC`,
`OMEGA_SWIR_C`/`OMEGA_SWIR_L` -- see the dedicated sections below for
ISS and OMEGA). Decision: extended `p.phocube` (new `-c` flag), not
`p.cam2map` -- research this session found `p.cam2map`'s actual code is
pure ellipsoid flat-field resampling despite its docs claiming SPICE
support (same doc/implementation mismatch `-s` mode fixed earlier), and
`p.phocube` already has the right per-input-pixel backplane shape plus
(from items 1-2) the kernel-history/line_rate/DSK machinery to reuse.
`-c` requires `instrument=` (v1: `CRISM_VNIR`/`CRISM_IR` only -- each
further instrument needed its own per-instrument camera-model research,
as expected) and `band=` (defaults to the IK's own reference band; later
dropped, see below -- the real geometry turned out band-independent).
Added
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

(Renamed `INSTRUMENT["ISS_WA"]` -> `ISS_WAC` shortly after, for
consistency with `p.in.archive`'s `sensor=CASSINI_ISS_WAC` and
`p.phocube`'s `instrument=` options -- see below.)

**Resolved (later session) -- this was a workflow issue, not a missing
automation**: `/usr/lib/grass/addons/scripts/p.spice.find` is owned by
the `grass-planetary-addons` Debian package (confirmed via `dpkg -S`),
and `/usr/local/grass86/scripts/p.spice.find` is already a symlink into
it (`postinst` creates this for every `p.*` script). `postinst` also
already self-heals from a bare `make install`'s stale real files (it
explicitly deletes any non-symlink `p.*` file in `GISBASE/scripts/`
before relinking). The staleness this session hit earlier came from
mixing two install paths -- editing source then `make
MODULE_TOPDIR=/usr/local/grass86 install` directly (bypassing the
package) -- instead of going through `dpkg-buildpackage -us -uc -b` +
`sudo dpkg -i ../grass-planetary-addons_*.deb`, which re-syncs
everything (including the system-wide addon copy) in one step. No code
change needed; just use the dpkg build+install workflow consistently
after this point rather than bare `make install` for anything meant to
stick.

### Cassini ISS NAC/WAC camera model added to `p.phocube -c`

Following the AWS-mirror indexing above, implemented `instrument=
ISS_NAC`/`ISS_WAC` -- a structurally different, harder shape than
CRISM's 1-D pushbroom pinhole:

- **Real 2-D framing geometry**: both `sample` and `line` are genuine
  focal-plane offsets (`dx`/`dy`), one static boresight per whole frame
  (no per-line gimbal CK, unlike CRISM -- added an `is_framing` flag to
  the camera struct so CRISM's `line` axis, which is *time*, doesn't get
  misread as a focal-plane offset -- caught and fixed before any testing
  by inspection, not by a failed run).
- **Real radial lens distortion** (`K1`): confirmed the exact formula
  from ISIS3's own `RadialDistortionMap.cpp`
  (`isis/src/base/objs/RadialDistortionMap/`): `ux=dx*(1+K1*r2)`,
  `uy=dy*(1+K1*r2)` -- closed-form, no iteration needed for the forward
  (sample -> ray) direction this module needs.
- **A custom IAK-defined frame**, not the bare NAIF one:
  `CASSINI_ISS_NAC_USGS`/`_WAC_USGS`, a 180 deg Z-rotation fixing a real,
  documented missing rotation in NAIF's own `cas_v*.tf`. Resolvable
  automatically once the IAK is `furnsh`'d via `ik=` (confirmed live --
  no new loading machinery needed).
- **Focal length genuinely varies per filter-wheel pair** (dozens of
  `INS-8236{0,1}_<F1>_<F2>_FOCAL_LENGTH` IAK keys, e.g. `CL1_CL2`,
  confirmed key order matches the real PDS3 label's `FILTER_NAME` tuple
  order exactly). Added a `filter_name` capture to `p.in.archive.py`'s
  OPUS ISS import (the Python `p_meta.PlanetaryMetadata.filter_name`
  field already existed in the schema but nothing populated it for ISS
  until now) and a `p.phocube` auto-read of it via the *already-generic*
  `p_meta_read_string_field()` (no `libs/p_meta` C changes needed at all
  -- that reader does a flat `strstr` over the whole JSON file, so it
  already worked for a field nested under `extended_metadata.planetary`
  without modification). `filter1=`/`filter2=` CLI options override it;
  the IAK's own `DEFAULT_FOCAL_LENGTH` is the documented last-resort
  fallback (its own comment: "not being used... but was left in").

Renamed `CrismCameraModel` -> `PinholeCameraModel` and
`load_crism_camera_model()` -> `load_pinhole_camera_model()` (now shared
by CRISM and ISS); removed nothing from CRISM's path (`is_framing=0`,
`k1=0` for it, `boresight_line` stays unused as before).

**Verified end-to-end against two real Cassini ISS frames**, both
confirmed via OPUS's own `SURFACEGEOsaturn_rangetobody1` field (Saturn's
surface actually in view) *before* downloading -- the first two
real-target candidates tried (`N1498508609_1`, `N1508882636_1`, both
locally cached from an earlier session) turned out to be pointed ~4.5
deg off Saturn's disk (real archived "TARGET_NAME=SATURN" images are not
guaranteed to be disk-centered; NAC's FOV is only 0.35 deg), so the OPUS
metadata check was the fix, not a guess:

- **NAC** (`co-iss-n1466182140`, 2004-06-17, filter `P0/CB2` -- not in
  the IAK, exercises the `DEFAULT_FOCAL_LENGTH` fallback): 100% pixel
  hit rate (0/262144 NULL), southern-hemisphere-only latitudes (-34.0 to
  -0.9 deg), emission 0.03-34.5 deg, incidence 66.5-92.7 deg.
- **WAC** (`co-iss-w1466182067`, same observation sequence, filter
  `CB2/IRP0` -- exact IAK match): WAC's wide FOV captures the whole disk
  at this 8.29M km range -- 41694/1048576 pixels hit (~4%, matching
  Saturn's small angular size relative to the frame), full -180..179.7
  deg longitude coverage, latitudes -89.6 to +67.9 deg.

Both verified kernel sets cached at `~/RSDATA/Saturn/spice_test/` and
`~/RSDATA/Misc/{N1466182140_1_CALIB,W1466182067_1_CALIB}.{LBL,IMG}`;
added `test_camera_mode_real_iss_nac_geometry`/`_wac_geometry` to
`p.phocube`'s test suite (both passing, ~25s combined).

**Fixed: the `p.spice.find` CK/SPK trailing-edge date-matching bug**
(found above). `_best_ck()`/`_best_spk()` matched candidates by *date
only* (`r[0] <= target_date <= r[1]`), not time-of-day. Real Cassini
"ra" reconstructed CK/SPK files are released in fixed-cadence windows
(e.g. ~5 days) whose *actual data* coverage runs from day-N 00:01 to
day-(N+5) 00:01 -- NOT through the end of day N+5 despite the filename
implying otherwise (confirmed via `ckcov_c`/`spkcov_c` directly against
real CASSINI archive files). Requesting a time late in the *last* day
of a window's filename range silently selected that window instead of
the *next* one (which actually covers it), because both nominally match
per filename-date overlap and the matcher didn't break the tie by
coverage risk.

Fix: added `_trailing_edge_risk(r, target_date)` (returns 1 iff
`target_date` is exactly the window's *last* nominal day), inserted as
a tie-breaker between the existing type/SCPSE preference and span
preference in both `_best_ck()`'s and `_best_spk()`'s candidate sort
keys -- `(type_score, edge_risk, span, f)` / `(scpse_bonus, edge_risk,
span, f)`. Deliberately a tie-breaker, not an exclusion: a
trailing-edge candidate is still returned when it's the only match
(e.g. a target genuinely in the first few minutes of that day).
Verified against the exact real dates that exposed the bug
(`p.spice.find spacecraft=CASSINI time=2005-297T21:35:08 kernels=ck,spk
-l`, live against naif.jpl.nasa.gov): now correctly selects
`05297_05302ra.bc`/`051024BP_SCPSE_05296_05306.bsp` instead of
`05292_05297ra.bc`/`050802R_SCPSE_05169_05186.bsp`. Added
`test_best_ck_avoids_trailing_edge_of_window`,
`test_best_spk_avoids_trailing_edge_of_window`, and
`test_best_ck_still_prefers_shortest_span_away_from_edge` (confirms the
existing shortest-span preference is unaffected when there's no real
edge risk) -- all 22 of `p.spice.find`'s tests pass, including the
5 live network ones.

**Fixed (later session)**: MEX's real CK
(`ATNM_MEASURED_YYMMDD_YYMMDD_VNN.BC`) and SPK
(`MEX_ROB_YYMMDD_YYMMDD_NNN.BSP`) filename conventions don't have their
date pair at the start of the filename like every other supported
mission, so no existing `_file_date_range()` regex matched them.
Fixed by adding `_RE_YYMMDD_YYMMDD_ANYWHERE` (`re.search`, tried last,
after the start-anchored patterns) to find a `YYMMDD_YYMMDD` pair
anywhere in the name. A second, previously-unknown bug was found and
fixed at the same time: every extension/prefix check in the module
(`_best_ck`, `_best_spk`, `_latest_file`) was case-sensitive
lowercase-only (e.g. `f.endswith(".bc")`), which silently broke MEX
entirely regardless of the regex fix -- MEX's whole real archive uses
uppercase filenames/extensions (`.TSC`, `.TI`, `.TF`, `.BC`, `.BSP`),
unlike CASSINI/MRO's lowercase convention. All three functions are now
case-insensitive; verified this doesn't change CASSINI/MRO selection
(regression test `test_cassini_lowercase_unaffected_by_case_fix`).

Filled in `SPACECRAFT["MEX"]["fk"] = "MEX_V*"` (was `None`) and added
an `INSTRUMENT["OMEGA_SWIR_C"/"OMEGA_SWIR_L"]` entry
(`ik: "MEX_OMEGA_V03.TI"`) so `instrument=` auto-selects OMEGA's real
public IK. `sclk`/`ik` stay `None` at the `SPACECRAFT` level (MEX ships
a single un-dated SCLK; IK is per-instrument).

Verified end-to-end live: `p.spice.find spacecraft=MEX
instrument=OMEGA_SWIR_C time=2004-02-10T18:08:35
kernels=lsk,sclk,ik,fk,pck,spk,ck -l` now auto-selects all 7 real
kernels used in the OMEGA verification above (`naif0012.tls`,
`MEX_260522_STEP.TSC`, `MEX_OMEGA_V03.TI`, `MEX_V16.TF`,
`PCK00010.TPC`, `MEX_ROB_040101_041231_003.BSP`,
`ATNM_MEASURED_040101_050101_V03.BC`) -- previously these had to be
hand-`curl`'d one by one. Added 8 offline unit tests and a
`TestNetworkMex` live-network test class (5 tests) to
`p.spice.find`'s test suite; all 35 tests pass (19 offline, 16
network), including the pre-existing CASSINI/IAK ones (confirmed
unaffected).

### MEX OMEGA SWIR-C/SWIR-L camera model added to `p.phocube -c`

Following plan item 3 (this repo's own `calm-spinning-pearl` plan,
"Cassini ISS" item -- OMEGA was explicitly deferred there as "not
actionable" since no IAK exists on the ISIS3 AWS mirror, see above).
Revisited from scratch against the real public NAIF/ESA IK instead of
an IAK, since OMEGA genuinely has none:

- **A third, structurally different camera shape**: not a pinhole
  focal-plane map at all (unlike CRISM/ISS), but a real whiskbroom
  scanning mirror. Per `MEX_OMEGA_V03.TI`'s own "OMEGA Pixels Geometry"
  section: each pixel's pointing is the "central" pixel vector
  (boresight, `(0,0,1)` in the detector's own frame) rotated about the
  detector frame's `+Y` axis by `offset_angle = (dn_position -
  MIRROR_CENTER_POSITION) * MIRROR_SLOPE` degrees, where `dn_position`
  is the *real* per-sample scanning-mirror position (DN) -- not
  synthesized, not assumed constant. `MIRROR_CENTER_POSITION=512`,
  `MIRROR_SLOPE=0.0092243187` deg/DN, read from the IK under the shared
  SWIR id (`INS-41420_*`), not the per-channel SWIR-C/SWIR-L id, since
  one physical mirror serves both InSb arrays.
- **A genuine pre-existing `libs/p_pds` correctness bug, found as a
  side effect of this research, not previously known**: real archives
  disagree on band-suffix ("backplane") sideplane row width. Cassini
  VIMS pads each row to `(samples + suffix_sample_items)` items
  (confirmed against ISIS3's own `vims2isis/main.cpp::ReadVimsBIL()`
  source, found by searching `$HOME/dev/ISIS3` per this repo's global
  CLAUDE.md instruction); MEX OMEGA's real archived QUBE uses exactly
  `samples` items per row -- confirmed via exact
  `(FILE_RECORDS-LABEL_RECORDS)*RECORD_BYTES` byte-count arithmetic
  against a real downloaded file (`ORB0100_0.QUB`: 39973 records, 512
  bytes/record, 11 label records, 424 lines -> exactly 48256 bytes/line,
  vs the VIMS-style-assumed 48284 -- a difference of exactly
  `suffix_band_items * suffix_item_bytes` = 7*4=28 bytes). This silently
  affected every line beyond line 0 of every previous OMEGA import.
  Fixed via a new `PPdsImage.line_stride_bytes` field: when the label
  provides `FILE_RECORDS`/`RECORD_BYTES`/`LABEL_RECORDS` with
  `RECORD_TYPE=FIXED_LENGTH`, the real per-line byte stride is derived
  from those (ground truth) instead of assumed, in both
  `p_pds_read_row()`'s BIL case and the new
  `p_pds_read_band_suffix_row()` -- generic and label-driven, not a
  hardcoded per-mission branch. Added
  `test_omega_style_suffix_narrower_than_vims` to `libs/p_pds`'s test
  suite (9/9 passing).
- **New `p.in.pds3 suffix_band=` option**: `p.phocube`'s camera mode
  only has access to already-imported GRASS rasters, not the original
  PDS3 file -- so OMEGA's per-sample mirror-DN housekeeping data
  (embedded only in the QUBE's own band-suffix sideplane) was not
  reachable at all without this. `suffix_band=1` (1-based) imports one
  sideplane as its own raster (raw values, no `OFFSET`/
  `SCALING_FACTOR`), via the new `p_pds_read_band_suffix_row()` API.
- **A real architectural workaround, not a hack**: the real FK
  (`MEX_V16.TF`) centers `MEX_OMEGA_SWIR_C`/`_SWIR_L`'s frame on the
  `MEX_OMEGA` instrument body (-41400), which has no SPK ephemeris of
  its own (a fixed-mount instrument id, not a tracked body) --
  `sincpt`'s `dref` handling needs that center body's state regardless
  of aberration correction (`"LT"` alone still failed, not just
  `"LT+S"`'s stellar-aberration term). Since both `MEX_OMEGA_SWIR_C`
  and `_SWIR_L` are plain fixed-angle TKFRAMEs relative to
  `MEX_SPACECRAFT` (`_SWIR_L` via `_SWIR_C`), the fix is a one-time,
  time-independent `p_spice_pxform()` call at camera-model load time to
  pre-rotate into `MEX_SPACECRAFT` (whose center, -41, has real
  ephemeris throughout) and pass that as `dref` to `sincpt` instead --
  exact, not approximate, since TK frames have no light-time dependency.
- VNIR deliberately deferred: its own per-pixel mirror-DN equivalence
  for synced-acquisition products (where VNIR's sample count is forced
  to match SWIR's, per the EAICD) isn't yet verified -- still a real
  open question, not solved by this session's SWIR work.

**Verified end-to-end against a real MEX OMEGA EDR** (`ORB0100_0.QUB`,
orbit 100, 2004-02-10T18:07:10, 424 lines x 64 samples x 352 bands; real
kernels fetched directly from `naif.jpl.nasa.gov/pub/naif/MEX/kernels/`
-- `naif0012.tls`, `MEX_260522_STEP.TSC`, `MEX_OMEGA_V03.TI`,
`MEX_V16.TF`, `MARS_IAU2000_V0.TPC`, `pck00010.tpc`,
`MEX_ROB_040101_041231_003.BSP`, `ATNM_MEASURED_040101_050101_V03.BC`;
plus real NAIF generic kernels `de432s.bsp` and `mar099.bsp`, needed
because the real reconstructed-orbit SPK only gives MEX relative to
MARS (499), not all the way to the solar system barycenter that
`sincpt`/`ilumin` need). Using the cube's own real mid-scene epoch
(`2004-02-10T18:08:35.0475`) and real per-line cadence
(`line_rate=0.401002358`, derived from `(STOP_TIME-START_TIME)/LINES`):
100% pixel hit rate (0 NULL), and computed lat/lon bounds match the
label's own ground truth (`MAXIMUM_LATITUDE=-70.253`,
`MINIMUM_LATITUDE=-78.167`, `EASTERNMOST_LONGITUDE=303.019`,
`WESTERNMOST_LONGITUDE=291.415`) to within ~0.05 deg:

| | computed | label |
|---|---|---|
| max lat | -70.253 | -70.253 |
| min lat | -78.135 | -78.167 |
| east lon | 302.969 | 303.019 |
| west lon | 291.477 | 291.415 |

The cross-track rotation sign convention (`sin(theta)` for the `+X`
component) matched the real ground truth on the first try -- no
empirical sign-flipping was needed. Added
`test_camera_mode_real_omega_swir_c_geometry` to `p.phocube`'s test
suite (passing, ~5s); real kernels + cube cached at
`~/RSDATA/Mars/spice_omega/` and `~/RSDATA/Mars/ORB0100_0.QUB`.
