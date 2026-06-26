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
  nor the NASA PDS Federated Search. **4 science targets catalogued**:
  Mawrth Vallis (FRT00003BFB), Nili Fossae (FRT00003E12), Jezero Crater
  (FRT000047A3), Gale Crater (FRT0000901A) -- 8 entries total (IR + VNIR
  for each). Footprints verified via MTRDR map-projected labels.
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
| Mars | OMEGA | Mars Express | VIS+NIR imaging spectrometer, 0.38-5.1 um | **done** -- `omega=` in `p.in.archive`; 8-orbit catalog (orb0100–orb2500 + Tharsis/N.polar cap/Hellas additions) |
| Mars | MAVEN/IUVS | MAVEN | UV spectrograph (upper atmosphere) | NASA PDS Atmospheres Node |
| Moon | M3 (Moon Mineralogy Mapper) | Chandrayaan-1 | VNIR imaging spectrometer, 0.43-3.0 um | NASA PDS Imaging Node (also via Astropedia's own M3 mosaic in some contexts -- check STAC first) |
| Moon | LAMP | LRO | Far-UV spectrograph (airglow/exosphere) | **done** -- `lamp=` in `p.in.archive`, NASA PDS Imaging Node |
| Venus | VIRTIS | Venus Express | Imaging spectrometer, 0.25-5.1 um | **done** -- `virtis_vex=` in `p.in.archive`, ESA PSA |
| Venus | IR1/IR2/UVI | Akatsuki | Multispectral imagers (not hyperspectral, but UV-VIS-NIR) | **done** -- `akatsuki=` in `p.in.archive`, JAXA DARTS |
| Mercury | MASCS (UVVS+VIRS) | MESSENGER | UV-VIS-NIR point spectrometer | NASA PDS Geosciences Node |
| Mercury | MERTIS | BepiColombo (en route/ongoing) | Thermal-IR imaging spectrometer | not yet archived -- check ESA PSA as mission progresses |
| Saturn system | UVIS | Cassini | UV spectrograph (rings, atmospheres) | **done** -- `opus_id=co-uvis-euv*/co-uvis-fuv*` auto-infers `CASSINI_UVIS_EUV`/`CASSINI_UVIS_FUV` sensor in `p.in.archive` |
| Saturn system | VIMS | Cassini | Imaging spectrometer, 0.35-5.1 um | **done** -- `vims=` with 10-entry curated catalog (Titan x3, Enceladus x2, Saturn x2, rings, Iapetus, Dione) |
| Jupiter system | NIMS | Galileo | Near-IR imaging spectrometer | **done** -- `nims=` in `p.in.archive`, NASA PDS Imaging Node |
| Pluto/Charon + Arrokoth | LEISA (on Ralph) | New Horizons | IR imaging spectrometer | **done** -- `leisa=` in `p.in.archive`, NASA PDS SBN |
| (152830) Dinkinesh, (52246) Donaldjohanson | LEISA (on L'Ralph) | Lucy | IR imaging spectrometer | **done** -- `leisa=lucy_*` catalog entries added, `mission=LUCY`, `sensor=LUCY_LEISA` |
| Jupiter | JunoCam | Juno | 4-color visible imager | **done** -- `juno=` in `p.in.archive`, PDS Imaging Node; 3-entry catalog (JOI, PJ1, PJ7) |
| Vesta/Ceres | VIR | Dawn | VIS+IR imaging spectrometer | **done** -- `dawn_vir=` in `p.in.archive`, NASA PDS Small Bodies Node (SBN) |
| 67P/C-G | VIRTIS | Rosetta | Imaging spectrometer | **done** -- `virtis_rosetta=` in `p.in.archive`, ESA PSA |
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

**Full feasibility survey of the remaining 15 candidates** (4 parallel
research passes, real live-archive checks, no fabricated URLs -- see
also the auto-memory note `project_hyperspectral_survey.md` for the
full per-instrument detail): found 10 real, tractable candidates
(implementing one by one, this order):

- **Tractable now, same detached/attached-label PDS3 QUB+LBL pattern
  CRISM/M3/VIMS/OMEGA already use**: Dawn/VIR (**done**, see below),
  Venus Express/VIRTIS (**done**), Rosetta/VIRTIS (**done**),
  Galileo/NIMS (**done**, see below).
- **Trivial (not new engineering)**: Cassini/UVIS (already reachable
  via the existing `opus=`/`opus_id=` machinery -- just needs a
  `co-uvis-euv*`/`co-uvis-fuv*` sensor-prefix mapping addition);
  Cassini/VIMS dedicated catalog (already fully implemented, just has
  one curated entry -- "done" means adding more).
- **Tractable via the EXISTING generic GDAL-FITS import path, not a
  new `libs/p_pds` reader**: **done** -- MAVEN/IUVS (`iuvs=`), LRO/LAMP
  (`lamp=`), Akatsuki IR1/IR2/UVI (`akatsuki=`), New Horizons/LEISA
  (`leisa=`). All four are real FITS files with a detached PDS3-style or
  PDS4 XML label -- imported via `r.in.gdal` with FITS subdataset paths
  where needed (`FITS:<file>:1` for IUVS and LEISA which have multi-HDU
  layout; direct path for Akatsuki UVI which GDAL auto-promotes to the
  first IMAGE extension). Multi-band cubes (IUVS: 60 bands, LEISA: 270
  bands) get an imagery group via `i.group`; single-band images (LAMP:
  1024×32, Akatsuki: 1024×1024) are imported as single rasters.
- **Wrong product shape (point spectrometers, confirmed, not just
  assumed)**: MESSENGER/MASCS (likely a PDS3 TABLE, not yet fully
  confirmed), OSIRIS-REx/OVIRS (confirmed PDS4 point spectrometer, 4
  mrad FOV), Hayabusa2/NIRS3 (confirmed PDS4 + FITS + point
  spectrometer, 0.1 deg FOV).
- **Not archived yet, confirmed (not just assumed)**: BepiColombo/MERTIS
  (real flyby data exists, reported at conferences, but ESA's own
  MERTIS page says data is not yet public; Mercury orbit insertion not
  until Nov 2026), Europa Clipper/MISE (launched Oct 2024, still in
  cruise, zero Europa data yet), JUICE/MAJIS (real cruise-phase data
  exists -- e.g. a comet 3I/ATLAS outgassing detection, Nov 2025 -- but
  ESA states all MAJIS data goes to the PSA in 2029, nothing public
  yet).

### Dawn/VIR added to `p.in.archive` (`dawn_vir=`)

First of the 10 queued candidates. Real archive: NASA PDS Small Bodies
Node static tree (`sbnarchive.psi.edu/pds3/dawn/vir/`). Detached-label
PDS3 QUBE: `AXIS_NAME = (BAND,SAMPLE,LINE)` (-> BIP organisation, a case
`libs/p_pds` already handles generically), zero `SUFFIX_ITEMS`, 32-bit
`IEEE_REAL` spectral radiance (`W/(m**2*sr*micron)`) -- no `libs/p_pds`
changes needed at all, a plain case the existing reader already
supports. Two curated catalog entries added, one per Dawn target body:
`ceres_vir_ir_507093102` (Ceres LAMO, 2016-01-26) and
`vesta_vir_ir_380500497` (Vesta LAMO, 2012-01-22), both 432 bands x 256
samples x 48 lines. Verified live: real HTTP 200, `Content-Length`
matching the label's own `CORE_ITEMS` exactly
(432\*256\*48\*4 = 21233664 bytes for both), real end-to-end import via
`p.in.archive dawn_vir=<key> output=...` producing sane, non-degenerate,
physically-plausible-falloff radiance (band 1 mean 0.50, band 200 mean
0.025 W/(m^2 sr um)). `sensor=DAWN_VIR_IR`/`DAWN_VIR_VIS` (detected from
the real `VIR_IR_1B`/`VIR_VIS_1B` filename convention) correctly reaches
`planetary.json` via the same load-existing-record-and-update-in-place
fix `crism=`/`m3=`/`omega=` already needed (`p_meta.write_planetary_metadata()`
is create-only and `p.in.pds3 -g` already writes a generic record first).
No camera model added to `p.phocube -c` yet -- VIR's real per-pixel
boresight/IK geometry is a separate, not-yet-investigated task.

### Venus Express VIRTIS added to `p.in.archive` (`virtis_vex=`) -- new `libs/p_pds` BIP+suffix support

Second of the 10 queued candidates, and the first one to actually need a
real `libs/p_pds` reader extension (Dawn/VIR didn't). Real archive: ESA
Planetary Science Archive, `archives.esac.esa.int/psa/ftp/VENUS-EXPRESS/
VIRTIS/VEX-V-VIRTIS-2-3-V2.0/DATA/MTP<NNN>/VIR<NNNN>/RAW/`. Attached-label
PDS3 QUBE (single `.QUB`, `^QUBE` points at a record offset, same
convention OMEGA already taught this reader). Real layout:
`AXIS_NAME = (BAND,SAMPLE,LINE)` (BIP) with a genuine, nonzero
`SUFFIX_ITEMS = (0,10,0)` -- 10 16-bit housekeeping items per line. This
is a **different suffix attachment model than BIL's** (OMEGA/VIMS): in
BIL the suffix is extra bytes tacked onto every band-row; in BIP here the
10 suffix items are 10 *phantom whole-spectrum samples* appended once at
the end of each line (i.e. the per-real-sample stride is plain
`bands*item_bytes`, and only the line-to-line stride widens by
`suffix_sample_items` extra sample-slots). Confirmed via two independent
checks: (1) cross-referencing ISIS3's own `ProcessImportPds`/
`ProcessImport::ProcessBip()` source (`$HOME/dev/ISIS3`) for the
authoritative suffix-as-extra-samples model, confirming `AXIS_NAME =
(BAND,SAMPLE,LINE)` always maps to BIP organisation in real archives
regardless of physical channel; (2) direct empirical byte-level
decoding of a real downloaded VIRTIS-H sample (`VH0023_00.QUB`) at
several sample/line offsets, checking for smooth, physically-coherent
sample-to-sample and line-to-line spectral continuity -- the first,
wrong implementation attempt (treating the suffix as BIL-style
per-sample bytes) produced wildly discontinuous ±32767 garbage, caught
immediately by this check before it ever reached a "looks plausible"
state.

Implementing this also surfaced and fixed a real, pre-existing,
previously-undetected `libs/p_pds` bug: `scan_past_ascii()` (the
heuristic that detects and corrects stale attached-label `^IMAGE`/`^QUBE`
pointers) used a single-byte ASCII/whitespace test, so a real 16-bit
pixel whose high byte coincidentally fell in the printable-ASCII range
(e.g. `0x09`, a real VIRTIS-H raw DN's high byte) caused a false-positive
"still inside label text" detection, silently shifting the read offset
by a few bytes and corrupting every value. Fixed by requiring a short
*run* of `P_PDS_ASCII_RUN` (4) consecutive ASCII-like bytes before
concluding "still in label text" -- real PVL label text always runs many
consecutive printable bytes, while real binary data hitting one
ASCII-range byte by chance is expected and must not trigger a shift.
This fix is general (benefits every attached-label archive this reader
handles, not just VIRTIS) and was verified against the existing
`p_pds`/`test_p_pds.c` unit suite (9/9 passing, run with `GISBASE`/
`GISRC` set -- the test binary needs a real GRASS session for
`G_warning()` to behave; running it bare without env vars silently
`exit(1)`s on the very first `G_warning()` call, a pre-existing harness
quirk unrelated to this fix, confirmed by reproducing it against the
pre-change `p_pds.c` too).

Two curated catalog entries added (orbit 23, both real, separately
downloadable products from the same orbit): `orb0023_vh_00` (VIRTIS-H,
432 bands x 256 samples x 7 lines) and `orb0023_vi_00` (VIRTIS-M-IR, 432
bands x 256 samples x 35 lines). Verified live: real HTTP 200, real
end-to-end import via `p.in.archive virtis_vex=<key> output=...`
producing sane, non-degenerate raw DN (VIRTIS-H band 1 mean 4872, band
200 mean 8013; VIRTIS-M-IR band 1 mean 8013 -- all well within the
label's own saturation bounds, no garbage swings). `sensor=
VEX_VIRTIS_H`/`VEX_VIRTIS_M_IR` (detected from the real `VH`/`VI`
filename prefix convention) correctly reaches `planetary.json` via the
same load-existing-record-and-update-in-place fix `crism=`/`m3=`/
`omega=`/`dawn_vir=` already needed.

### Galileo NIMS added to `p.in.archive` (`nims=`) -- new `libs/p_pds` VAX_REAL + BSQ-suffix support

Fourth of the 10 queued candidates. Real archive: NASA PDS Imaging Node
static tree (`planetarydata.jpl.nasa.gov/img/data/go-j-nims-3-tube-v1.0/
go_<orbit>/<body>/`). Attached-label PDS3 QUBE (single `.qub`, `^QUBE`
at a record offset). Real layout: `AXIS_NAME = (SAMPLE,LINE,BAND)` (BSQ),
`CORE_ITEMS = (20,17,10)`, `CORE_ITEM_BYTES = 4`,
`CORE_ITEM_TYPE = VAX_REAL`, `SUFFIX_ITEMS = (0,0,12)` (12 geometry/
housekeeping backplane bands: lat/lon, projected line/sample, angles,
intercept altitude, native time).

Required two real `libs/p_pds` extensions (a new record for this candidate
-- prior ones each needed at most one):

1. **VAX F-float (32-bit) support** (`P_PDS_DTYPE_VAX_FLOAT32`): the
   most exotic pixel type encountered so far -- a 1960s-era DEC/VAX
   floating-point format, not IEEE 754. File bytes `[B0,B1,B2,B3]` form
   the logical 32-bit value as `(B1<<24)|(B0<<16)|(B3<<8)|B2` (two
   little-endian 16-bit words, high word first). The exponent bias is 128
   vs IEEE's 127, so the conversion is: rearrange bytes as above, then
   subtract `0x00800000` from the 32-bit value to adjust the bias -- one
   cheap integer subtraction, no iteration. Edge cases: exp=0 maps to
   zero (VAX dirty-zero); exp=0xFF (all PDS3 NIMS special pixels use
   this) maps to a sentinel NaN rather than a huge IEEE negative (all
   PDS3 NIMS special values -- NULL=`16#FFFFFFFF#`, LOW_REPR_SAT,
   HIGH_INSTR_SAT, etc. -- have VAX exponent=0xFF and are deliberately
   beyond any valid science data range). The NaN sentinel is caught by
   the existing `is_special_dn()` NaN/Inf check added during this work.
   `is_msb` set to 0 so the generic byteswap path is bypassed; the
   custom word-swap + bias-adjust is done entirely inside `dn_to_double()`.

2. **BSQ with band-suffix**: `SUFFIX_ITEMS = (0,0,12)` means 12 additional
   band-plane backplanes appended after all 10 core band planes. Existing
   code refused BSQ with any suffix at all (the guard previously only
   allowed BIL or BIP+sample-suffix). Added `bsq_ok = (org==BSQ &&
   sfx_s==0 && sfx_l==0)` to the guard -- BSQ core-band reads already
   worked correctly (the band planes are contiguous and the backplanes
   simply follow after all core bands, so seeking to core band b at
   `data_offset + b*band_size` is unaffected). Only the guard update was
   needed; no seek-offset change for core reads.

Three curated catalog entries added: `go1104_europa_g1e001ti` (Europa,
10 bands, 20 samples x 17 lines), `go1104_callisto_g1c001ti`,
`go1104_ganymede_g1g001ci`. Verified live: real HTTP 200, full
end-to-end import via `p.in.archive nims=<key> output=...` producing
sane, physically-plausible BDRF values (band 1 mean 0.895, band 3 mean
0.577, band 10 mean 0.052 for Europa -- consistent with Europa's known
near-IR reflectance drop from water-ice absorption). VAX special pixels
correctly nulled (bands 5-10 have progressively more null cells where the
instrument data was missing/saturated, all with sane non-null means
0.01-0.12). Data files correctly placed in `$RSDATA/Europa/` (body-aware
download path). `sensor=GALILEO_NIMS`, `mission=GALILEO` correctly written
to `planetary.json` via the standard load-existing-record-and-update-in-
place pattern.

### Rosetta VIRTIS added to `p.in.archive` (`virtis_rosetta=`)

Third of the 10 queued candidates. Real archive: ESA Planetary Science
Archive, mission path segment **`INTERNATIONAL-ROSETTA-MISSION`**, not
`ROSETTA` (a 404 trap the memory note's guessed URL fell into --
discovered by browsing the real directory tree from
`archives.esac.esa.int/psa/ftp/` down). Same attached-label PDS3 QUBE /
`AXIS_NAME = (BAND,SAMPLE,LINE)` BIP-with-sample-suffix layout as Venus
Express VIRTIS -- the `libs/p_pds` BIP+suffix fix from that candidate
applies directly, zero further library changes needed. Two real
differences from VEx worth noting: (1) Rosetta's VIRTIS-H is a true
1-sample-wide point-spectrometer slit (`CORE_ITEMS` sample=1, 3456
bands -- 8 spectral orders x 432, vs. VEx's 256-sample-wide H slit);
(2) Rosetta's byte accounting for the H-channel sample matches
`FILE_RECORDS` with **zero** residual (unlike VEx's small ~200-400 byte
trailing-padding residual), independently reinforcing that the BIP+
suffix model implemented for VEx is the right one. Two curated catalog
entries added, from the same orbit/session (STP013, MTP006):
`stp013_vh_s1_00366708038` (VIRTIS-H, 3456 bands x 1 sample x 18 lines)
and `stp013_vi_i1_00366679117` (VIRTIS-M-IR, 432 bands x 256 samples x
105 lines). Verified live: real HTTP 200, real end-to-end import via
`p.in.archive virtis_rosetta=<key> output=...` producing sane,
non-degenerate raw DN (VIRTIS-H band 1 mean 14284, band 1000 mean 3288;
VIRTIS-M-IR band 1 mean 834). `sensor=RO_VIRTIS_H`/`RO_VIRTIS_M_IR`
(detected from the real `S1_`/`I1_` filename prefix convention)
correctly reaches `planetary.json` via the same load-existing-record-
and-update-in-place fix the other catalog entries already needed.

### MAVEN IUVS added to `p.in.archive` (`iuvs=`)

Fifth of the 10 queued candidates. Real archive: NASA PDS Atmospheres Node
(`atmos.nmsu.edu`). Product type: **multi-HDU FITS** with a companion PDS4
XML label -- NOT a PDS3 binary. Import path: `r.in.gdal` via the GDAL FITS
driver, addressing HDU 1 (calibrated radiance cube, kR/nm) with subdataset
path `FITS:<file>:1`. Two catalog entries added: FUV limb scan
(`limb_orbit00107_fuv`) and MUV limb scan (`limb_orbit00107_muv`), orbit 107
of MAVEN, 2014-10-18, 60 bands × 165 spectral × 7 spatial pixels.
`r.in.gdal` does NOT auto-create a GRASS imagery group (unlike `p.in.pds3
-g`); must call `i.group` explicitly after import. Fixed in response to user
feedback: "Do not forget that all import of multi-bands image should create
an imagery group by the name of the file". `sensor=MAVEN_IUVS_FUV` or
`MAVEN_IUVS_MUV` inferred from filename substring (`-fuv_` / `-muv_`).

### LRO LAMP added to `p.in.archive` (`lamp=`)

Seventh of the 10 queued candidates. Real archive: NASA PDS Imaging Node
(`planetarydata.jpl.nasa.gov/img/data/lro/lamp/edr/`). Product type: **multi-
HDU FITS EDR** with a detached PDS3 label. GDAL sees the file as having
subdatasets; HDU 1 = door-open spectrogram (1024 spectral × 32 spatial
pixels), HDU 2 = door-closed background. Import via `r.in.gdal` with subdataset
path `FITS:<file>:1`. Single-band result (1 GDAL band from a 2D FITS array)
-- no imagery group. Verified: min=0, max=25,560 raw counts. Two catalog
entries from 2022-03-06 in LROLAM_0050. `sensor=LRO_LAMP`.

### Akatsuki (VCO) UVI added to `p.in.archive` (`akatsuki=`)

Eighth of the 10 queued candidates. Real archive: JAXA DARTS
(`data.darts.isas.jaxa.jp/pub/pds3/vco-v-uvi-3-cdr-v1.0/`). Product type:
**multi-HDU FITS** where the primary HDU has NAXIS=0 (no data) and the first
IMAGE extension is the science image. GDAL automatically promotes to the first
IMAGE extension, so no `:N` subdataset suffix needed -- direct path import
works. 1024×1024 calibrated radiance in W/m²/sr/m, single band (one filter
per file). Sensor inferred from filename: `uvi_` → `AKATSUKI_UVI`, `ir1_` →
`AKATSUKI_IR1`, `ir2_` → `AKATSUKI_IR2`, `lir_` → `AKATSUKI_LIR`. Three
catalog entries: UVI 283 nm and 365 nm from orbit 1 (2015-12-07), UVI 365 nm
from orbit 8 (2016-02-24). Verified: min=−3.4e+38 (missing-data sentinel from
`P_MPIXV` header keyword), max=25.7 MW/m²/sr/m. `mission=AKATSUKI`.

### New Horizons LEISA added to `p.in.archive` (`leisa=`)

Ninth of the 10 queued candidates. Real archive: NASA PDS Small Bodies Node
(`pds-smallbodies.astro.umd.edu/holdings/nh-a-leisa-3-kem2-v1.0/`). Product
type: **multi-HDU FITS** 3D cube, NAXIS=3, 256×256×270 bands, BITPIX=−32
(IEEE float). Like IUVS, GDAL reports subdatasets and requires the subdataset
path `FITS:<file>:1` to access the science cube (otherwise refuses: "No raster
bands found"). Import via `r.in.gdal FITS:<file>:1` creates 270 GRASS raster
bands at once; then `i.group` creates the imagery group. Spectral coverage:
1.25–2.50 µm (near-IR). Target: Arrokoth (MU69, KEM2 approach phase).
Verified: band 1 max=0.34, band 135 max=0.43 (physically plausible NIR
reflectance); −9999 fill value used for unobserved pixels. `sensor=NH_LEISA`.
`mission=NEW_HORIZONS`.

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

Status: **implemented and verified correct against real data for nine
instruments** (`CRISM_VNIR`/`CRISM_IR`, `ISS_NAC`/`ISS_WAC`,
`OMEGA_SWIR_C`/`OMEGA_SWIR_L`/`OMEGA_VNIR`, `VIMS_IR`/`VIMS_VIS` -- see
the dedicated sections below for ISS, OMEGA, and VIMS). Decision:
extended `p.phocube`
(new `-c` flag), not
`p.cam2map` -- research found `p.cam2map`'s actual code was pure
ellipsoid flat-field resampling despite its docs claiming SPICE support
(same doc/implementation mismatch `-s` mode fixed earlier), and
`p.phocube` already has the right per-input-pixel backplane shape plus
(from items 1-2) the kernel-history/line_rate/DSK machinery to reuse.
`p.cam2map` itself was later given a real camera-model **back**-
projection (`-c`, ISS_NAC/ISS_WAC only) -- see "Candidate #5" below.
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
- **`OMEGA_VNIR` added (later session)**: confirmed against a real
  cube (`ORB0100_0.QUB`: `CHANNEL_ID=(IRC,IRL,VIS)`, `CORE_ITEMS`
  sample=64 -- identical to SWIR's sample count, not VNIR's native
  384/128-pixel pushbroom width) that the only product type currently
  importable is synced-acquisition: VNIR shares SWIR's real
  per-line/per-sample mirror telemetry one-for-one, so the identical
  `mirror_dn=`/`offset_angle` formula applies, just rotated out of
  `MEX_OMEGA_VNIR`'s own detector frame (a fixed ~0.3 deg TKFRAME
  offset from `MEX_OMEGA_SWIR_C`, per `MEX_V16.TF`) instead of SWIR's.
  No code changes to the ray-construction loop were needed -- the
  existing `is_omega` path is already frame-agnostic; only a new
  `instrument=OMEGA_VNIR` branch in `load_pinhole_camera_model()` was
  added. Verified: 100% pixel hit rate, lat/lon bounds within ~0.2 deg
  of `OMEGA_SWIR_C`'s own label-verified bounds on the same cube --
  consistent with the two channels' real, small boresight offset (no
  independent VNIR ground truth exists in the label). The
  native-resolution, unsynced 128-pixel VNIR pushbroom mode
  (`MEX_OMEGA_V03.TI`'s `INS-41410_PIXEL_DN` calibration table) remains
  a different, currently non-importable product type -- not
  implemented; revisit if/when `p.in.archive`/`p.in.pds3` gains a path
  for it. Added `test_camera_mode_real_omega_vnir_geometry` to
  `p.phocube`'s test suite (passing).

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

### Cassini VIMS_IR/VIMS_VIS camera model added to `p.phocube -c`

Following plan item 4 (`calm-spinning-pearl` plan -- VIMS was deferred
there since its IAK, `vimsAddendum04.ti`, only fixes
`CK_FRAME_ID`/`NAIF_BODY_CODE` housekeeping, no boresight/focal-length).
Revisited from scratch by reading ISIS3's own `VimsCamera`/
`VimsGroundMap` source (`$HOME/dev/ISIS3/isis/src/cassini/objs/
VimsCamera/`, per this repo's global CLAUDE.md instruction to search
`$HOME/dev/` for third-party source before guessing):

- **A fourth, structurally different camera shape**: not a focal-plane
  mm pinhole at all (unlike CRISM/ISS), and not OMEGA's whiskbroom
  either -- a real 2-axis angular scan, output directly as a unit look
  vector in spherical terms. Ported verbatim from
  `VimsGroundMap::LookDirection()`:
  `x = sample + camSampOffset; y = line + camLineOffset; theta = pi/2 -
  (y-yBore)*yPixSize; phi = -pi/2 + (x-xBore)*xPixSize; v = (sin(theta)
  *cos(phi), cos(theta), -sin(theta)*sin(phi))`. `xPixSize`/`yPixSize`/
  `xBore`/`yBore` and the *integer* `camSampOffset`/`camLineOffset`
  (truncating division reproduced exactly, matching ISIS3's own `int`
  arithmetic) depend on channel (IR/VIS) x SamplingMode (NORMAL/HI-RES)
  -- 4 real, hardcoded combinations confirmed straight from
  `VimsCamera.cpp`'s constructor and `VimsGroundMap.cpp`'s `Init()`.
- **Confirmed neither the public IK nor the IAK has a usable boresight
  model**: `cas_vims_v06.ti` only gives the overall FOV envelope
  (`FOV_REF_ANGLE`/`FOV_CROSS_ANGLE` = 0.9167 deg half-angle) and a
  nominal 64x64 pixel grid (`FOV_CENTER_PIXEL = (31.5, 31.5)`) -- no
  per-pixel angle formula like OMEGA's `MIRROR_SLOPE`.
- **A real, documented VIMS_IR/VIMS_V NAIF ID swap bug, confirmed
  directly from the IAK's own comment**: the public IK assigns
  `CASSINI_VIMS_IR=-82370`/`CASSINI_VIMS_V=-82371`; the public FK
  (`cas_v43.tf`) assigns the *opposite* (`CASSINI_VIMS_V=-82370`/
  `CASSINI_VIMS_IR=-82371`) in its actual `FRAME_-8237{0,1}_NAME`
  definitions (confirmed by reading both real kernels directly, not
  just an early naming-table comment in the FK that turned out to be a
  red herring from an earlier ID scheme). `vimsAddendum04.ti`'s own
  comment: "There is also a problem within the cassini ik kernels where
  VIMS_IR has code (-82370) and VIMS_V has code (-82371)... a
  discrepency within the kernels and until resolved we will be using
  the parallel array definition below" -- i.e. the IAK is the
  authority: `CASSINI_VIMS_V=-82370`, `CASSINI_VIMS_IR=-82371` (matching
  the FK's actual definitions). `p.phocube` uses these corrected IDs;
  `ik=cas_vims_v06.ti,vimsAddendum04.ti` (both, in that order) is
  required for `instrument=VIMS_IR`/`VIMS_VIS` to resolve correctly.
- **New per-cube metadata fields, not from any kernel**: VIMS's real
  `SamplingMode`/`XOffset`/`ZOffset`/`SwathWidth`/`SwathLength` live
  only in the PDS3 label's Instrument group
  (`SAMPLING_MODE_ID = ("<IR mode>","<visible mode>")` -- confirmed
  index order against ISIS3's own `vims2isis/main.cpp::
  TranslateVimsLabels()` --, plus `X_OFFSET`/`Z_OFFSET`/`SWATH_WIDTH`/
  `SWATH_LENGTH`, shared by both channels). Added
  `sampling_mode_ir`/`sampling_mode_vis`/`x_offset`/`z_offset`/
  `swath_width`/`swath_length` fields to `p_meta.PlanetaryMetadata`
  (written as JSON strings, even the ints -- `p_meta_read_string_field()`
  on the C side is a minimal quoted-string-only scanner, not a real
  JSON parser), a new `_pds3_vims_geometry()` regex parser in
  `p.in.archive.py` (mirrors `_pds3_filter_pair()`'s existing pattern
  for ISS), wired into the OPUS `vims=`/`opus=` import's existing
  load-update-save metadata block. `p.phocube -c` reads them back via
  `p_meta_read_string_field()`, or via new `sampling_mode=`/`x_offset=`/
  `z_offset=`/`swath_width=`/`swath_length=` CLI overrides when
  importing by some other path (mirrors ISS's `filter1=`/`filter2=`
  pattern).
- `cam.frame` is the plain NAIF frame (`CASSINI_VIMS_IR`/
  `CASSINI_VIMS_V`) -- both have real ephemeris (`FRAME_-8237{0,1}
  _CENTER = -82`, the orbiter itself, not a fixed-mount instrument body)
  -- no `pxform` workaround needed, unlike OMEGA's `-41400` issue.

Renamed the camera-overrides parameter list into a `CameraOverrides`
struct (`filter1`/`filter2`/`sampling_mode`/`x_offset`/`z_offset`/
`swath_width`/`swath_length`) in `p.phocube/main.c`, since
`load_pinhole_camera_model()`'s positional-argument list was growing
unwieldy with VIMS's five new override fields on top of ISS's two.

**Verified end-to-end against a real Cassini VIMS cube**
(`v1799424623_1.qub`, the T-108 Titan flyby, 2015-01-08T15:09:40.135;
real kernels fetched via `p.spice.find spacecraft=CASSINI
instrument=VIMS kernels=lsk,sclk,ik,fk,pck,spk,ck,iak` --
`naif0012.tls`, `cas00172.tsc`, `cas_vims_v06.ti`, `vimsAddendum04.ti`,
`cas_v43.tf`, `cpck_rock_21Jan2011_merged.tpc` + `pck00010.tpc` (needed
separately -- the mission-dir PCK doesn't carry Titan's `BODY606_RADII`),
`150108AP_SCPSE_14365_15016.bsp`, `15008_15013ra.bc`). IR channel
(HI-RES) and VIS channel (NORMAL), same swath
(`X_OFFSET=11 Z_OFFSET=25 SWATH_WIDTH=38 SWATH_LENGTH=18`): both land
on real, physically sane, smoothly-varying, *overlapping* (not
identical -- different boresight/pixel-pitch/SamplingMode, exactly as
expected for two co-mounted, simultaneously-acquired channels) patches
of Titan's disk:

| | IR (HI-RES) | VIS (NORMAL) |
|---|---|---|
| lat range | -65.23 .. 68.09 deg | -64.93 .. 74.68 deg |
| lon range | -62.32 .. 104.54 deg | -30.36 .. 92.88 deg |
| incidence | 9.5 .. 106.7 deg | -- |
| emission | 7.6 .. 76.4 deg | -- |
| pixel hit rate | 50/684 (~7.3%) | 22/684 (~3.2%) |

The real label has no precomputed footprint geometry to check against
(unlike OMEGA's `MAXIMUM_LATITUDE` etc.), so there's no independent
ground truth here -- but the smooth, continuous, disk-shaped (not
scattered/random) non-NULL pattern, the physically sane incidence/
emission ranges, and the overlapping-but-distinct IR/VIS coverage are
all real, falsifiable signs of a correct model, not just crash-free
output. Added `test_camera_mode_real_vims_ir_geometry`/
`_vis_geometry` to `p.phocube`'s test suite (both passing, ~5s
combined), with the bounds above frozen as the regression baseline;
real kernels + cube cached at `~/RSDATA/Saturn/spice_vims/` and
`~/RSDATA/Misc/v1799424623_1.{qub,lbl}`.

### Candidate #5: `p.cam2map`/`p.caminfo` doc/implementation mismatch -- full rebuild

`p.cam2map` and `p.caminfo` both had docs claiming real SPICE camera
geometry (named map projections, incidence/emission/phase, footprint
vectors) but their actual code was pure ellipsoid flat-field lat-lon
remapping -- no `p_spice` calls at all. User explicitly chose the "full
rebuild: real camera-model back-projection in `p.cam2map`" option (not
just fixing docs, not just adding SPICE angles to `p.caminfo`, which
remains untouched/still mismatched -- a future candidate if wanted).

**Design**: new `-c` flag on `p.cam2map` reuses `p.phocube -c`'s exact
ISS_NAC/ISS_WAC pinhole+K1-distortion+per-filter-focal-length camera
model (read from the same real ISIS3 IAK). For each OUTPUT pixel (real
lat/lon), the algebraic inverse of `p.phocube`'s forward ray
construction recovers the input (sample, line): `latsrf_c` gets the
body-fixed surface point; `spkpos_c(target=BODY, et, fixref, "LT+S",
observer=SPACECRAFT)` gives `-spacecraft_pos_in_fixref`; ray =
spoint+pos; rotate into the camera frame; scale to focal_length; invert
the K1 radial distortion via 5 fixed-point iterations; sample/line from
boresight+dx|dy/pixel_pitch. CRISM/OMEGA/VIMS are explicitly **not**
supported -- their time/sample-varying pointing needs a 1-D/2-D
root-search inverse, not a closed-form one.

**Architectural fix**: a real `PROJECTION_LL` GRASS location
hard-enforces +-90 deg latitude at the C library level, making it
impossible to import a raw camera image taller than 180 rows (a typical
raw frame, whose native region treats row index as a coordinate) into
the same location as a real-CRS output -- and this GRASS version has no
public cross-location raster-read API. Resolution: run `-c` in a
`PROJECTION_XY` location (matching this project's own established
convention -- every camera-mode real-data test in `p.phocube`'s test
suite already does this), with the output region's bounds interpreted
directly as real lat/lon degrees by this module's own code.

**Three real bugs found and fixed during verification against a real
Cassini ISS NAC frame of Saturn** (`N1466182140_1_CALIB`,
`INSTRUMENT_MODE_ID=SUM2`, 2004-06-17, filter P0/CB2):

1. `Rast_get_d_row()`/`Rast_put_d_row()` resample against the raster
   library's own window cache (`R__.rd_window`/`wr_window`), which is
   **distinct** from the GIS library's `G__.window` and is only synced
   by `Rast_set_window()` -- `G_set_window()` alone leaves it stale at
   whatever it was lazily initialised to on the first raster open. This
   caused a `malloc(): corrupted top size` crash in `Rast_put_d_row`
   (the output file's header was sized from the wrong, stale window).
   Fixed: use `Rast_set_window()`, not `G_set_window()`, when switching
   between the input's native region and a different output region in
   one process -- a pattern novel to `p.cam2map` (`p.phocube` never
   resamples between two different regions in one run).
2. **Missing SUMMING/binning correction**: `BORESIGHT_SAMPLE`/
   `BORESIGHT_LINE`/`PIXEL_PITCH` in the IK/IAK are given for the
   detector's full (1x1) resolution (1024x1024 for ISS NAC/WAC), but
   this real test frame is 2x2-binned (512x512,
   `INSTRUMENT_MODE_ID=SUM2`) -- using the raw IK values directly
   against the binned image's own pixel coordinates silently misplaced
   every ray by the summing factor (confirmed: a 0% back-projection hit
   rate even at the frame's own forward-computed centre lat/lon). Fixed
   in **both** `p.cam2map` and `p.phocube` (this is a real,
   previously-undetected bug in `p.phocube`'s own established ISS
   camera model too -- its existing test only checked hit-rate and
   hemisphere sign, neither of which is sensitive to this offset) by
   detecting the summing factor automatically: compare the IK's own
   full-frame `INS<id>_PIXEL_SAMPLES`/`PIXEL_LINES` to the image's
   actual dimensions, then rescale `boresight_sample`/`boresight_line`/
   `pixel_pitch` accordingly. No new CLI option needed.
   `test_camera_mode_real_iss_nac_geometry` in `p.phocube`'s test suite
   was updated: its old `assertLess(lat_max, 0)` ("southern hemisphere
   only") assumption was itself a symptom of this bug and is no longer
   true with the corrected geometry (now correctly straddles the
   equator) -- replaced with a tighter, still-meaningful check (lat
   range within +-90 deg and a fairly narrow span, consistent with this
   frame's ~0.35 deg FOV).
3. **Missing light-time epoch handling in the inverse rotation**:
   `spkpos_c`'s own docs state that for a "received radiation" abcorr
   (`"LT+S"`) and a non-inertial output frame, the frame's orientation
   is evaluated at `et-lt`, not `et`. The original inverse used a
   single `pxform(fixref, camera_frame, et)` call for both legs, which
   silently assumed `fixref`'s orientation at `et` (not `et-lt`) --
   wrong by Saturn's own rotation over the one-way light time (~27.6 s
   at this frame's ~8.29M km range; ~0.26 deg of Saturn rotation,
   comparable to the NAC's entire ~0.35 deg FOV). Fixed: decompose the
   rotation through the (epoch-independent) inertial J2000 frame as an
   intermediate, with each leg evaluated at its own correct epoch
   (`pxform(fixref, "J2000", et-lt)` then `pxform("J2000", camera_frame,
   et)`) -- confirmed via a direct round-trip check (forward
   centre-pixel lat/lon -> `-c` back-projection on a tiny 1x1 deg region
   at that exact lat/lon) that this recovers the original (sample,
   line) almost exactly (within ~1 pixel) once both fixes were in
   place.

**Verified**: 100% round-trip hit rate at the frame's own forward
centre pixel; 57.4% hit rate (non-degenerate, real disk-shaped coverage)
over the frame's full forward lat/lon extent. Added
`test_camera_mode_real_iss_nac_round_trip` to `p.cam2map`'s test suite
(passing); real kernels + cube reused from `p.phocube`'s own ISS
fixture (`_find_iss_test_kernels()`, `~/RSDATA/Saturn/spice_test/` and
`~/RSDATA/Misc/N1466182140_1_CALIB.*`).

Rewrote `p.cam2map.md` to remove the false `projection=`/`res=`/
`clon=`/8-named-projections/`p_projection_planet` claims and document
the real `-c` ISS_NAC/ISS_WAC back-projection feature, the
`PROJECTION_XY` convention, and the summing/light-time handling.

**Cartographic map-projection support added (2026-06-26):** Added
`projection=` option (`latlon` default, `sinusoidal`, `stereo_north`,
`stereo_south`) and `clon=` to `p.cam2map`. The projection inverse is
applied in the output pixel loop before the `latsrf_c` call. Sinusoidal
at the equator is algebraically identical to `latlon`, verified by
regression test (`test_sinusoidal_projection_matches_latlon_at_equator`).
Docs updated in `p.cam2map.md`.

### Candidate #5b: `p.caminfo`'s own doc/code mismatch -- full rebuild

`p.caminfo`'s docs claimed real SPICE camera geometry (calling a
`p_spice_geo_row` function that does exist in `libs/p_spice` but was
never wired up) -- centre/corner lat/lon, incidence/emission/phase,
sub-solar/sub-spacecraft points, solar distance, pixel resolution,
north azimuth -- but the actual code just computed generic ellipsoid
radius/resolution stats from CLI-supplied `a_radius`/`b_radius`/
`c_radius`, no SPICE calls at all, and even the option name differed
from every other module (`map=` instead of `input=`). User asked for
this to be closed out the same way as candidate #5 (`p.cam2map`): a
full rebuild matching the docs, not a docs-only fix.

ISIS3's own `caminfo` (the documented "ISIS3 equivalent") is itself a
raw-camera-cube report tool -- it evaluates the real camera model at
the image centre/corners, not a georeferenced product's region. So
`p.caminfo` was rebuilt to reuse the exact same per-instrument pinhole
camera model `p.phocube -c`/`p.cam2map -c` already use (boresight/
pixel-pitch/focal-length/K1, including the SUMMING/binning auto-
detection fix from candidate #5), evaluated at 5 points (centre + 4
corners) instead of every pixel. Scoped to `CRISM_VNIR`/`CRISM_IR`/
`ISS_NAC`/`ISS_WAC` only (same reasoning as `p.cam2map`'s scope: MEX
OMEGA's whiskbroom mirror needs a per-pixel `mirror_dn=` raster lookup
and Cassini VIMS's 2-axis scan needs real per-cube swath offsets --
both are mechanically addable later but skipped here to keep this
candidate reviewable).

**New library functions** (`libs/p_spice`): `p_spice_subpnt()`/
`p_spice_subslr()`, wrapping CSPICE `subpnt_c`/`subslr_c` (sub-observer
and sub-solar point) with this library's existing simple
"Ellipsoid"/"DSK/Unprioritized" method-string convention (mapped
internally to the longer `subpnt_c`/`subslr_c` method strings). Reused
the already-existing `p_shape_xyz_to_latlon()` (`libs/p_shapemodel`) to
convert SPICE's body-fixed XYZ surface points to planetocentric
lat/lon, rather than adding a redundant `reclat_c` wrapper.

**North azimuth** (not previously implemented anywhere in this
project) is computed as the clockwise angle, in the image's own
(sample, line) plane, from "up" (decreasing line) to true north at the
centre pixel: project the body's rotation-pole direction onto the
local tangent plane (using the centre-to-surface-point direction as an
approximate outward normal -- exact for a sphere, a small
approximation for a flattened ellipsoid, consistent with this
project's other documented-approximation formulas), then rotate into
the camera frame through the same two-leg, light-time-aware J2000
decomposition fixed in candidate #5 (`pxform(fixref, "J2000", trgepc)`
then `pxform("J2000", camera_frame, et)`, using `sincpt`'s own returned
`trgepc` directly rather than recomputing `et-lt`).

**Verified against real data, cross-validated against two
independently-already-verified sources**:
- `ISS_NAC` (same `N1466182140_1_CALIB` SUM2 frame as candidate #5):
  centre lat/lon -10.61/317.49 deg, matching the independently-computed
  `p.phocube -c`/`p.cam2map -c` forward geometry for this exact frame
  (-10.58/-42.60, i.e. 317.40 in 0-360 convention) to within ~0.1 deg.
  Solar distance 9.04 AU (Saturn's real heliocentric distance) and
  pixel resolution ~99.3 km/pixel (matches `pixel_pitch/focal_length *
  range` computed by hand) are both real, physically sane numbers, not
  just non-crashing output.
- `CRISM_VNIR` (same `FRT00003BFB` cube as `p.phocube`'s own CRISM
  test): centre lat/lon 22.148/342.044 deg (i.e. -17.96 in +-180
  convention), matching the known Mawrth Vallis ground truth
  (22.149/-17.95) used as that test's own regression baseline, to
  within 0.01 deg. Solar distance 1.509 AU (a real, in-range Mars
  heliocentric distance).

Added `test_camera_mode_real_crism_geometry`/
`_iss_nac_geometry` to `p.caminfo`'s test suite (both passing). Rewrote
`p.caminfo.md` to describe the real centre/corner camera-ray geometry,
renamed the `map=` option to `input=` (matching `p.phocube`/
`p.cam2map`'s convention), and removed the dead `a_radius`/`b_radius`/
`c_radius` options (no longer used -- real radii now come implicitly
from the loaded PCK via the SPICE calls themselves). `Makefile` updated
to link `p_spice`/`p_meta`/cspice (mirrors `p.cam2map`'s Makefile).

### p.caminfo extended to OMEGA_SWIR_C/SWIR_L/VNIR and VIMS_IR/VIMS_VIS

Extended `p.caminfo` to cover the two remaining camera shapes previously
noted as "not yet wired" (see candidate #5b note above):

- **MEX OMEGA (whiskbroom scanning mirror)**: same `mirror_center`/
  `mirror_slope`/`omega_rot` model and IK kernel-pool reads as
  `p.phocube -c`. The 5-point (centre + 4 corners) evaluation needs the
  mirror-DN telemetry at each specific (sample, line) position -- supplied
  via the new `mirror_dn=` raster option (the band-suffix sideplane
  imported via `p.in.pds3 suffix_band=1`). `Rast_get_d_row()` is called
  once per evaluation point rather than once per whole image row, making
  the 5-point path much cheaper than `p.phocube`'s per-pixel path.
  IFOV for resolution: `mirror_slope * pi/180` rad/pixel.

- **Cassini VIMS (2-axis angular scan)**: same `vims_*` geometry struct
  and `VimsGroundMap::LookDirection()` formula as `p.phocube -c`. Swath
  metadata (`sampling_mode=`, `x_offset=`, `z_offset=`, `swath_width=`,
  `swath_length=`) read from the raster's `planetary.json` when imported
  via `p.in.archive vims=`, or overridable via CLI options. IFOV:
  `vims_x_pixsize` rad/pixel.

- **`CamPoint` extended**: added `trgepc` field (the surface epoch
  returned by `sincpt`), so the north-azimuth computation no longer
  needs a second `sincpt` call to recover it. All instrument types
  benefit; fixes a latent inefficiency in the CRISM/ISS paths too.

Verified against the same real fixtures used by `p.phocube`'s own tests:

- `OMEGA_SWIR_C` (ORB0100_0.QUB, orbit 100, Mars, 2004-02-10): centre
  lat/lon lands within the label's declared extents (-78.167..-70.253 N,
  291.415..303.019 E); solar distance 1.5x AU (real Mars range); pixel
  resolution > 0. `mirror_dn=` raster from `p.in.pds3 suffix_band=1`.
- `VIMS_IR` (v1799424623_1.qub, T-108 Titan, 2015-01-08): solar distance
  within 7-12 AU (Saturn range); centre hit with lat/lon within the
  verified patch (-70..75 N); resolution > 0.

Added `test_camera_mode_real_omega_swir_c_geometry` /
`test_camera_mode_real_vims_ir_geometry` to `p.caminfo`'s test suite
(both passing, ~6s combined).

## 4. Next work items — simplest first, hardest last

### 4-A. Catalog expansions (pure Python, zero new C/SPICE code)

These are all dictionary entries in `p.in.archive.py`. No new import
handlers, no new `libs/p_pds` work. Simplest possible change — just
real URL verification + adding dict entries + doc update.

**4-A-1. More CRISM observations** -- **done** -- Jezero, Nili Fossae, Gale added

Added 6 new entries (IR + VNIR for each of 3 new targets) to `CRISM_CATALOG`.
All footprints verified via MTRDR PDS5 labels (MINIMUM/MAXIMUM_LATITUDE,
WESTERNMOST/EASTERNMOST_LONGITUDE). Archive layout discovery: TRR3 index
files have null CENTER_LATITUDE for early 2007 data; reliable coordinates
only obtainable from individual MTRDR product labels.

- **Jezero Crater** FRT000047A3 (18.6N, 77.5E), 2007-02-26
  `mrocr_2101/trdr/2007/2007_057/` -- keys: `jezero_crater_frt000047a3_{ir,vnir}`
- **Nili Fossae** FRT00003E12 (22.3N, 77.1E), 2007-01-13
  `mrocr_2101/trdr/2007/2007_013/` -- keys: `nili_fossae_frt00003e12_{ir,vnir}`
- **Gale Crater** FRT0000901A (5.5S, 137.5E), 2007-12-27
  `mrocr_2102/trdr/2007/2007_361/` -- keys: `gale_crater_frt0000901a_{ir,vnir}`

**Hellas Basin rim** still not added (low priority, defer).

**4-A-2. More OMEGA orbits** -- **done**

Added 3 orbits to `OMEGA_CATALOG` (now 8 entries total), all HTTP 200 verified:
- `orb0331_2` (2004-04-23): Tharsis volcanic plateau, lat 11–33°N, lon 255–263°E;
  covers Ascraeus Mons (11.3°N, 256°E) and Ceraunius Tholus (24°N, 262°E).
  Found by scanning ORB03 segment labels: ORB0331 crosses lon ~260°E at equatorial latitudes.
- `orb0751_0` (2004-08-21): Northern polar cap during northern summer (H₂O residual cap
  fully exposed, CO₂ sublimated); lat 44–85°N, lon 143–202°E.
- `orb2204_0` (2005-10-01): Hellas Basin (deepest impact basin on Mars, centre ~42°S 70°E);
  lat -72 to -31°S, lon 37–121°E; dust storm occurrence and CO₂ frost.

**4-A-3. More VIMS observations** (currently 10 entries, all Saturn system)

Targets not yet in the catalog:
- **Phoebe** (irregular captured moon, 2004 flyby; dark primitive surface):
  the only dedicated VIMS/Phoebe flyby sequence.
- **Hyperion** (chaotic rotation, sponge-like morphology):
  VIMS observed spectral diversity within the chaotic rotation.
- **Rhea** (heavily cratered, largest airless moon after Iapetus):
  water ice / CO₂ ice surface.
- **Tethys** (Odysseus impact basin) -- ice + contaminant mapping.

All fetchable via `opus_id=co-vims-v<SCLK>` once the OPUS IDs are
verified against the ring node.

### 4-B. New missions — framing cameras (PDS3/FITS, no SPICE needed for import)

These need a new `<name>=` option and catalog, but no new `libs/p_pds`
engineering: either standard PDS3 detached-label images (handled by
existing `p.in.pds3`) or FITS (handled by `r.in.gdal`). Ordered by
increasing archive complexity.

**4-B-1. New Horizons/LORRI** (Long Range Reconnaissance Imager)

Single-band panchromatic framing camera. Calibrated RDR as PDS3
detached-label `.FIT` (FITS) or `.IMG`. PDS Small Bodies Node:
`pds-smallbodies.astro.umd.edu/holdings/nh-j-lorri-2-jupiter-v3.0/`
(Jupiter flyby 2007) and
`pds-smallbodies.astro.umd.edu/holdings/nh-p-lorri-3-plutosystem-v3.0/`
(Pluto system 2015).
Good catalog entries: Pluto highest-resolution pre-flyby image, Charon,
Hydra, Nix, Arrokoth approach, Jupiter atmospheric structure.
Import: `r.in.gdal` (FITS) or `p.in.pds3` (PDS3 `.IMG`); single raster
(one band), no imagery group needed.
Complexity: **very low** -- same import path as Akatsuki UVI.

**4-B-2. Galileo/SSI** (Solid State Imaging camera)

800×800 CCD framing camera, 8-bit PDS3 `.IMG` with attached label.
PDS Imaging Node:
`planetarydata.jpl.nasa.gov/img/data/galileo/` 
Catalog entries: Io volcanic plumes (closest approach 1999), Europa
surface ice (ice raft, lineae), Ganymede grooved terrain, Callisto
cratered surface. Note: many Galileo images suffered data compression
artifacts (the HGA antenna failed) -- catalog should preferably pick
the cleaner ones (higher telemetry rate passes).
Archive is standard PDS3 binary, `p.in.pds3` handles it.
Complexity: **very low** -- same as NIMS archive which is already done.

**4-B-3. Dawn/FC** (Framing Camera)

1024×1024 framing camera (7 colour filters + clear). Calibrated FITS
or PDS3. PDS Small Bodies Node:
`sbnarchive.psi.edu/pds3/dawn/fc/` (Vesta and Ceres).
Complements the Dawn/VIR already in the catalog.
Two targets, many volumes. Import: `r.in.gdal` (FITS) or `p.in.pds3`.
Complexity: **low** -- same archive host as Dawn/VIR, already proven
reachable.

**4-B-4. MESSENGER/MDIS** (Mercury Dual Imaging System)

WAC (12 colour filters, 1024×1024) + NAC (monochrome, 1024×1024).
PDS3 detached-label `.IMG`. PDS Geosciences Node:
`pds-geosciences.wustl.edu/messenger/mess-e_v_h-mdis-2-edr-rawdata-v1.0/`
(EDR) and `mess-e_v_h-mdis-5-rdr-image-v1.0/` (RDR).
Good catalog entries: Mercury color mosaics (first-colour imaging ever
of some terrains), Caloris Basin (giant impact structure), smooth plains,
hollows.
Complexity: **low-medium** -- new archive host (PDS Geosciences Node for
imaging, same host as CRISM but a different volume tree).

**4-B-5. Hayabusa2/ONC** (Optical Navigation Camera)

ONC-T (telescopic, 1024×1024, 7 band-pass filters), ONC-W1/W2 (wide,
1024×1024, monochrome). FITS. JAXA/DARTS:
`data.darts.isas.jaxa.jp/pub/hayabusa2/onc_bundle/` (PDS4 + FITS).
Target: Ryugu. Good catalog entries: global colour map, boulder field
near landing site, touchdown approach sequence.
Import: `r.in.gdal` (FITS), single or multi-band.
Complexity: **medium** -- new JAXA archive host, PDS4 label structure
differs from PDS3, may need label parsing for band metadata.

**4-B-6. MRO/CTX** (Context Camera)

Pushbroom, 5024 pixels wide, 6 m/px, monochrome, PDS3 IMG/LBL.
PDS Geosciences Node:
`pds-geosciences.wustl.edu/mro/mro-m-ctx-2-edr-l0-v1.0/`
Very large data volumes; each observation can be hundreds of MB.
Good catalog entries: Jezero Crater (context for CRISM + Perseverance),
Gale Crater, Valles Marineris cross-section.
Import: `p.in.pds3` (standard PDS3 binary).
Complexity: **medium** -- pushbroom data fills 100s MB per scene; the
import itself is straightforward but scene selection for the catalog
requires checking index files for specific targets.

### 4-C. Camera-model inverse back-projection extensions

These extend `p.cam2map -c` to non-ISS instruments. The forward direction
(`p.phocube -c`) already works for all of them. The inverse needs a
root-search rather than a closed-form formula, since CRISM's 1-D scan
and OMEGA's 2-D whiskbroom don't invert algebraically.

**4-C-1. `p.cam2map -c` for CRISM (1-D pushbroom inverse)**

For each output lat/lon, find the input (sample, line) where
CRISM's camera ray passes through that surface point.
- Cross-track (sample): invert `angle = a0(band) + a1(band)*sample`
  algebraically -- same closed-form as the forward model, just solved
  for `sample`. This part is actually trivial.
- Along-track (line): need to find which scan line's epoch `et(line)`
  points the instrument toward the target lat/lon. Binary search on
  `line` (monotone along-track motion), with `sincpt_c` at each
  candidate epoch. Convergence: ~10 iterations to sub-pixel.
- CK must be available (kernels in raster history via `p.spiceinit`).
- Per-output-pixel cost: ~10 `sincpt_c` calls (vs. 1 for ISS).
Complexity: **medium-high** -- new bisect loop, but reuses all existing
`p_spice` calls; no new library functions needed.

**4-C-2. `p.cam2map -c` for OMEGA (2-D whiskbroom inverse)**

For each output lat/lon, find the input (sample, line) where OMEGA's
scanning mirror ray passes through that surface point.
- Along-track (line): same binary search as CRISM (per-line epoch).
- Cross-mirror (sample): given a line epoch, invert the mirror-DN
  mapping `angle = center + slope * mirror_dn` for the `mirror_dn`
  that produces the target ray -- requires the mirror-DN sideplane
  raster (`mirror_dn=`) at the candidate line, which is a 1-D lookup
  once the line is known.
- Two nested root-searches (outer on line, inner on sample/mirror_dn).
  Convergence: slower than CRISM -- ~100 sincpt_c calls per output pixel.
- The `mirror_dn=` raster must be supplied (same as `p.phocube -c`).
Complexity: **high** -- two nested bisect loops, high per-pixel cost
for large output regions; may need spatial pre-filtering to avoid
evaluating all output pixels naively.
