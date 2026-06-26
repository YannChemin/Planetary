# p.in.archive - Fetch and import planetary data from real remote archives

## DESCRIPTION

*p.in.archive* searches for and imports planetary data products from
several real remote archives:

- **USGS Astropedia STAC** — the authoritative catalog of cartographic
  and image products from USGS Astrogeology Science Center, covering
  the Moon, Mars, Mercury, and many outer-planet bodies. Searched by
  default for `search=` and `doi=` queries.
  Endpoint: `https://stac.astrogeology.usgs.gov/api/`

- **NASA PDS Federated Search API** — the NASA Planetary Data System
  discovery portal, covering the full archive of mission data products
  identified by PDS4 Logical Identifiers (LIDs).
  Endpoint: `https://pds.nasa.gov/api/search/1/`

- **USGS COG mosaics** (`cog=`) — the classic global cartographic mosaics
  on `planetarymaps.usgs.gov` (e.g. the LOLA and MOLA global DEMs). These
  are **not** in the STAC catalog, but they serve HTTP byte ranges, so the
  module imports them through GDAL `/vsicurl/` and **windows the read to the
  active GRASS region** — no full-file download. Pass a catalog key (see the
  `-l` listing) or a direct `https` URL to a `.tif`.

- **OPUS (Ring-Moon Systems Node)** (`opus=` / `opus_id=`) — the PDS
  Ring-Moon Systems Node observation search and retrieval system, which
  is the primary archive for Cassini ring-science data including ISS and
  VIMS. The module queries the OPUS REST API, downloads the `.img` or
  `.qub` product file (resumable), and imports via `p.in.pds3`.
  Endpoint: `https://opus.pds-rings.seti.org/api`

- **MRO/CRISM TRDR products** (`crism=`) — the PDS Geosciences Node static
  archive (`pds-geosciences.wustl.edu`) is the *only* working source for
  CRISM Targeted RDR hyperspectral cubes: CRISM is absent from OPUS's
  instrument catalog (outer-planet/ring-science only — no MRO instruments
  at all) and CRISM TRDR products are **not indexed** in the NASA PDS
  Federated Search even by exact LID (confirmed empty `hits:0` against a
  real, verified LID). `crism=` fetches the `.IMG` + companion `.LBL`
  directly from this archive's `trdr/<year>/<doy>/<OBSID>/` tree (resumable,
  via `wget -c`) and imports the multi-band cube as a GRASS imagery group
  via `p.in.pds3 -g`. Pass a catalog key (see `-l`) or a direct `https` URL
  to a `.IMG`.

- **Chandrayaan-1 M3 L1B products** (`m3=`) — the JPL PDS Imaging Node
  static archive (`planetarydata.jpl.nasa.gov`) hosts Moon Mineralogy
  Mapper L1B radiance cubes as detached-label PDS3 products with a
  non-standard pointer (`^RDN_IMAGE`, nested inside an `OBJECT = RDN_FILE`
  wrapper, not the usual `IMAGE`/`QUBE`/`SPECTRAL_QUBE` object name —
  `libs/p_pds` gained a generic `*_IMAGE`/`*_QUBE` object-name fallback to
  read these). `m3=` fetches the `*_RDN.IMG` + companion `*_L1B.LBL` and
  imports the 85-band cube as a GRASS imagery group via `p.in.pds3 -g`,
  same as `crism=`. Pass a catalog key (see `-l`) or a direct `https` URL
  to a `*_RDN.IMG`. Verified end-to-end against a real product (real,
  non-degenerate per-band radiance, confirmed via `r.univar`).

- **Cassini VIMS** (`vims=`) — a dedicated shortcut into the `opus_id=`
  path below: resolves a catalog key (see `-l`) to a real OPUS
  observation ID and fetches both the VIS (96 bands, 0.35–1.05 µm) and
  IR (256 bands, 0.88–5.1 µm) channels as separate multi-band GRASS
  imagery groups. The `vims_channel=` option (default: `ir`) selects
  which channel to import. Per-cube camera-model metadata
  (`sampling_mode_ir`/`sampling_mode_vis`, `x_offset`, `z_offset`,
  `swath_width`, `swath_length`) is read from the PDS3 label and
  written into `planetary.json` automatically, ready for `p.phocube -c
  instrument=VIMS_IR`/`VIMS_VIS`. Ten curated entries covering Titan,
  Enceladus, Saturn, rings, Iapetus, and Dione are built in (see `-l`).

On download, the file format is detected by extension and dispatched
to the appropriate GRASS importer:

| Extension  | Importer     |
|------------|--------------|
| `.tif`/`.tiff` | `r.in.gdal` |
| `.fits`    | `r.in.gdal`  |
| `.img`     | `p.in.pds3`  |
| `.qub`     | `p.in.pds3`  |
| `.xml`     | `p.in.pds4`  |

## NOTES

### Raw instrument cubes (`crism=`/`m3=`/`vims=`/`omega=`) need an unprojected project

These four import **raw, non-georeferenced pixel/line instrument-frame
cubes** (that's the whole reason `p.phocube`/`p.spiceinit` exist -- to
attach real geometry to them afterwards), not map-projected products.
Their automatic region pre-sizing fails with `Illegal latitude for
North` if run inside a geographic (lat/lon) GRASS project -- use an
unprojected one, e.g. `grass --tmp-project XY --exec p.in.archive
crism=... output=...` for ad-hoc use. `cog=`/`doi=`/`lid=`/`search=`
products are unaffected (they're already map-projected).

### Spatial pre-filtering from the active GRASS region

On startup *p.in.archive* reads the current GRASS computational
region (equivalent to `g.region -p`) and extracts the longitude/latitude
bounding box [W, S, E, N]. This bbox is forwarded to both the STAC
`bbox` filter and the PDS API `bbox` parameter so that **only products
whose footprint intersects the active map window are returned**.

Use the **`-r` flag** to disable the spatial pre-filter and search globally.

```sh
# List Moon DEM products that overlap the active map window
p.in.archive -l search="LOLA DEM"

# Same search without spatial constraint
p.in.archive -lr search="LOLA DEM"
```

### USGS COG mosaics (`cog=`)

- The COG catalog is built in; list it with **`-l`** (no other source needed).
- `cog=` accepts either a catalog key or a direct HTTPS URL.
- `cog=` cannot be combined with `doi=`/`lid=`/`search=`/`opus=`/`opus_id=`.

Remote COGs are pre-downloaded with `wget -c` (resumable, 5 retries,
60-second timeout) into a per-body local cache before `r.import` is
invoked on the local copy. Cached files are reused whenever the local
size matches the remote `Content-Length`.

Pass `project=<NAME>` to auto-create a GRASS project at the source's
native CRS (read via `gdalsrsinfo`) and import into it directly — no
manual `g.proj` step needed.

### OPUS and VIMS (`opus=`, `opus_id=`, `vims_channel=`)

OPUS is the canonical discovery and download interface for Cassini ring
science data. Use it to fetch ISS raw images (`.img`) or VIMS
hyperspectral cubes (`.qub`):

```sh
# List recent Cassini VIMS observations of Saturn
p.in.archive -l opus="instrument=Cassini VIMS,target=Saturn" output=x

# Import VIS channel of the first result
p.in.archive opus="instrument=Cassini VIMS,target=Titan" \
    vims_channel=vis output=vims_titan_vis

# Import a specific VIMS observation by OPUS ID (IR channel)
p.in.archive opus_id=co-vims-v1799424623 vims_channel=ir output=vims_ir
```

VIMS cubes are PDS3 format; both `.qub` channels (VIS: 96 bands,
0.35–1.05 µm; IR: 256 bands, 0.88–5.1 µm) are imported via `p.in.pds3`
and registered as a multi-band imagery group (`output.1` .. `output.N`),
same convention as `crism=`/`m3=`. The companion `.lbl` label, and the
`.fmt` "structure" files real VIMS labels reference via `^STRUCTURE`
(`core_description.fmt`, `suffix_description.fmt`,
`band_bin_center.fmt`), are all fetched automatically — see "QUBE
sample-/band-suffix bytes" below for why the `.fmt` files matter.

The OPUS files API keys on the *exact* `_vis`/`_ir`-suffixed observation
id (confirmed live: querying the bare/unsuffixed id always returns no
files); `opus_files()` tries the id exactly as given first, then retries
with `vims_channel=`'s suffix appended if that was empty and the given id
had no suffix yet — fixed this session after finding the unsuffixed
lookup never actually worked.

### MRO/CRISM TRDR products (`crism=`)

```sh
# List the built-in CRISM catalog
p.in.archive -l

# Import the Mawrth Vallis FRT00003BFB VNIR cube (S detector, 107 bands)
p.in.archive crism=mawrth_vallis_frt00003bfb_vnir output=crism_mawrth_vnir

# Import the IR cube (L detector, 438 bands, 1.00-3.92 um)
p.in.archive crism=mawrth_vallis_frt00003bfb_ir output=crism_mawrth_ir

# Nili Fossae (22.3N, 77.1E) — olivine/carbonate/phyllosilicate terrain
p.in.archive crism=nili_fossae_frt00003e12_ir output=crism_nili_ir

# Jezero Crater (18.6N, 77.5E) — Perseverance landing site, delta phyllosilicates
p.in.archive crism=jezero_crater_frt000047a3_ir output=crism_jezero_ir

# Gale Crater (5.5S, 137.5E) — Curiosity landing site, smectite/sulfate strat.
p.in.archive crism=gale_crater_frt0000901a_ir output=crism_gale_ir

# Or pass a direct https URL to any other TRDR .IMG on the archive
p.in.archive \
    crism="https://pds-geosciences.wustl.edu/mro/mro-m-crism-3-rdr-targeted-v1/mrocr_2101/trdr/2007/2007_005/FRT00003BFB/FRT00003BFB_01_IF156L_TRR3.IMG" \
    output=crism_mawrth_ir
```

The catalog covers four key Mars mineralogy sites, each verified via
CRISM MTRDR map-projected label footprints (MINIMUM/MAXIMUM_LATITUDE,
WESTERNMOST/EASTERNMOST_LONGITUDE) against the known target coordinates:

| Key prefix | Target | Center lat/lon | Date |
|---|---|---|---|
| `mawrth_vallis_frt00003bfb` | Mawrth Vallis | 22.3°N, 342°E | 2007-01-05 |
| `nili_fossae_frt00003e12` | Nili Fossae trough | 22.3°N, 77.1°E | 2007-01-13 |
| `jezero_crater_frt000047a3` | Jezero Crater delta | 18.6°N, 77.5°E | 2007-02-26 |
| `gale_crater_frt0000901a` | Gale Crater (Mt Sharp) | 5.5°S, 137.5°E | 2007-12-27 |

Each site has `_ir` (L detector, IR 1.00–3.92 µm, 438 bands) and
`_vnir` (S detector, 0.36–1.05 µm, 107 bands) variants.

`output=` becomes a GRASS imagery group (`output.1` .. `output.N`, one
raster per spectral band). Downloads land in `~/RSDATA/Mars/` and are
resumable via `wget -c`.

CRISM detector naming: **L** = long-wavelength/IR detector (1.00–3.92 µm,
438 bands); **S** = short-wavelength/VNIR detector (0.36–1.05 µm,
typically 107–184 bands depending on the segment's spectral binning).
A given FRT (Full Resolution Targeted) observation ID can have multiple
repeat segments (`_01_`, `_02_`, `_03_`, ...) acquired during the same
multi-pass campaign; the wavelength-filter code (e.g. `156`, `166`)
varies per segment and cannot be derived from the observation ID alone —
verified by reading the MTRDR label footprints for each archived product.

Downloads land in `~/RSDATA/<Body>/` and are resumable via `wget -c`.

### Chandrayaan-1 M3 L1B products (`m3=`)

```sh
# List the built-in M3 catalog
p.in.archive -l

# Import the seed L1B radiance cube (orbit 141, 85 bands)
p.in.archive m3=m3g20081118t222604_v03_rdn output=m3_radiance

# Also fetch and import M3's precomputed per-pixel geometry
# (longitude/latitude/radius + illumination/viewing angles)
p.in.archive m3=m3g20081118t222604_v03_rdn -g output=m3_radiance
```

Verified live end-to-end: real HTTP 200 on `*_RDN.IMG`/`*_L1B.LBL`,
successful `p.in.pds3 -g` import (85 bands matching the label's own
`BANDS` count), and non-degenerate per-band radiance confirmed via
`r.univar` (e.g. band 1: -27 to 435 W/(m²·µm·sr)). `output=` becomes a
GRASS imagery group, same convention as `crism=`.

M3's L1B label uses a non-standard data-object name
(`OBJECT = RDN_IMAGE`, pointer `^RDN_IMAGE`, nested inside an outer
`OBJECT = RDN_FILE` wrapper) instead of the PDS3-standard
`IMAGE`/`QUBE`/`SPECTRAL_QUBE` — `libs/p_pds` gained a generic fallback
(any `*_IMAGE`/`*_QUBE` object with a matching `^<name>` pointer
anywhere in the label) to read these, found and fixed this session.

**Geometry companions (`-g`).** Unlike CRISM, whose TRDR product
carries no per-pixel geometry (hence `p.phocube -c`'s SPICE/camera-model
pipeline), M3's L1B label *also* describes two more image objects
side by side with `RDN_IMAGE`, each pointing at its own companion file
in the same archive directory:

- `LOC_IMAGE` (`*_LOC.IMG`, 3 bands) — per-pixel longitude, latitude,
  radius (selenocentric).
- `OBS_IMAGE` (`*_OBS.IMG`, 10 bands) — to-Sun/to-instrument azimuth and
  zenith angle, phase angle, to-Sun/to-instrument path length, facet
  slope, facet aspect, facet cos(i).

The `-g` flag fetches both companion files and imports each as its own
GRASS imagery group, `<output>_loc.1..3` and `<output>_obs.1..10`,
tagged in `planetary.json` as derived/ancillary data (`derived=true`,
`data_type=ancillary`) rather than raw radiance. This needed a new
`p.in.pds3 object=` option (selects a named image object out of a label
that describes more than one, since the default behaviour picks the
first match) — verified live: real longitude/latitude/radius/phase-angle
values for this same orbit, sane (radius 1734-1738 km, matching the
Moon; phase angle 82-98°).

### QUBE sample-/band-suffix bytes (VIMS, OMEGA)

Real PDS3 **QUBE** objects from some archives (Cassini VIMS, ESA Mars
Express OMEGA) carry non-zero `SUFFIX_ITEMS` — extra "sideplane"/
"backplane" bytes appended per sample- and band-direction record (e.g.
OMEGA's `SUFFIX_ITEMS = (1,7,0)`, VIMS's `(1,4,0)`). `libs/p_pds` skips
these correctly for the one layout actually observed in both real
archives: `BAND_STORAGE_TYPE = LINE_INTERLEAVED` (BIL) with a zero
line-suffix — a sample-suffix block is appended after each band's core
samples within a line, and a band-suffix backplane is appended once per
line after all bands. The byte layout was derived from, and verified
against, NASA's own ISIS3 production importer
(`ReadVimsBIL()` in `isis/src/cassini/apps/vims2isis/main.cpp`), then
confirmed directly against real downloaded `.qub` files (sane,
non-uniform per-band DN ranges; cross-checked against a parallel manual
`p.in.pds3` run). Any other QUBE organisation, or a nonzero
line-suffix, is still refused (`G_warning` + clean failure) rather than
guessed — this isn't a generic suffix-skipper, just the one real,
verified layout.

`libs/p_pds` also infers BIL automatically from
`AXIS_NAME = (SAMPLE,BAND,LINE)` when `BAND_STORAGE_TYPE` is absent
entirely (true for both OMEGA and VIMS — the QUBE convention is that
axis order *is* the storage order), and tolerates VIMS's real
object-name/pointer-keyword mismatch (`OBJECT = SPECTRAL_QUBE` but
`^QUBE = (...)`, not `^SPECTRAL_QUBE`).

**`^STRUCTURE` external "structure files".** VIMS labels factor
`CORE_ITEM_BYTES`/`CORE_ITEM_TYPE`/`CORE_NULL`/etc. out into small shared
`.fmt` files referenced via `^STRUCTURE = "core_description.fmt"`
instead of inlining them (OMEGA inlines everything directly — no
`^STRUCTURE` needed). Without resolving these, `p.in.pds3` silently fell
back to a wrong 8-bit-unsigned default instead of the real
16-bit-signed DN — no error, no warning, just wrong data. `libs/p_pds`
now splices in any `^STRUCTURE`-referenced file's keywords as if they
had been inlined; `vims=` fetches the `.fmt` files OPUS already
enumerates alongside the `.qub`/`.lbl`.

One real, benign edge case: the very last image line of real OMEGA/VIMS
cubes runs a few bytes short of a full line for the highest-numbered
bands. `p_pds_read_row()` reports this as a per-row read failure;
`p.in.pds3`'s `write_band()` turns that into a GRASS NULL row rather
than aborting — a small fraction of pixels NULL at one edge, not a crash.

### Mars Express OMEGA EDR products (`omega=`)

```sh
# List the built-in OMEGA catalog
p.in.archive -l

# Import a real Mars-orbit OMEGA EDR cube (orbit 100, 352 bands)
p.in.archive omega=orb0100_0 output=omega_orbit100
```

Attached-label PDS3 QUBE — a single `.QUB` file carries both label and
data, no companion `.LBL` to fetch. `output=` becomes a GRASS imagery
group, same convention as `crism=`/`m3=`/`vims=`. Verified live: real
352-band cube from `archives.esac.esa.int`, sane raw DN within the
label's own declared saturation bounds (-32768/32767).

The scanning mirror position sideplane (band-suffix index 1) needed by
`p.phocube -c instrument=OMEGA_SWIR_C/SWIR_L/OMEGA_VNIR` is **not**
imported by `omega=` itself — import it separately via:
```sh
p.in.pds3 input=<downloaded>.QUB output=omega_mirror_dn suffix_band=1
```

Five curated entries cover the MEX mission through December 2005:

| Key | Date | Geographic coverage | Science note |
|-----|------|---------------------|--------------|
| `orb0100_0` | 2004-02-10 | lat −78 to −70, lon 291–303 E | Early mission southern |
| `orb0331_2` | 2004-04-23 | lat 11 to 33 N, lon 255–263 E | Tharsis plateau — Ascraeus Mons, Ceraunius Tholus |
| `orb0511_0` | 2004-06-14 | lat −85 to −36, lon 281–326 E | Southern high-lat, winter polar hood |
| `orb0751_0` | 2004-08-21 | lat 44 to 85 N, lon 143–202 E | Northern polar cap, northern summer (H₂O residual cap) |
| `orb1000_0` | 2004-10-29 | lat 43 to 81, lon 35–190 E | Northern high-lat, Vastitas Borealis |
| `orb2001_0` | 2005-08-05 | lat −73 to −34, lon 155–276 E | Southern mid-lat, late winter |
| `orb2204_0` | 2005-10-01 | lat −72 to −31 S, lon 37–121 E | Hellas Basin (deepest impact basin, ~42°S 70°E) |
| `orb2500_0` | 2005-12-23 | lat −87 to −57, lon 193–358 E | Southern polar cap, summer CO₂/H₂O ice |

### New Horizons and Lucy LEISA products (`leisa=`)

```sh
# List the built-in LEISA catalog (both NH and Lucy entries)
p.in.archive -l

# Import the New Horizons Arrokoth cube (270 bands, 1.25-2.50 µm, ~350 MB)
p.in.archive leisa=nh_leisa_arrokoth_20181231 output=leisa_arrokoth

# Import a Lucy Dinkinesh flyby cube (270 bands, ~477 MB)
p.in.archive leisa=lucy_leisa_dinkinesh_02300 output=leisa_dinkinesh

# Import a Lucy Donaldjohanson flyby cube (270 bands, ~27 MB -- smaller)
p.in.archive leisa=lucy_leisa_donaldjohanson_02533 output=leisa_don
```

Calibrated PDS4 FITS science cubes from the PDS Small Bodies Node
(`pds-smallbodies.astro.umd.edu`). Import via `r.in.gdal FITS:<path>:1`
(HDU 1 is the calibrated science cube). `output=` becomes a GRASS
imagery group (`output.1` .. `output.270`). Mission metadata is written
into `planetary.json` automatically (`mission=NEW_HORIZONS` or
`mission=LUCY`, `sensor=NH_LEISA` or `sensor=LUCY_LEISA`).

The Donaldjohanson entry (`leisa=lucy_leisa_donaldjohanson_02533`) is
only ~27 MB and is the easiest starting point; the Dinkinesh and
Arrokoth cubes are 350–760 MB. Both Dinkinesh and Donaldjohanson are
main-belt asteroids visited by Lucy on its way to the Jupiter Trojans.

### Juno JunoCam raw EDR products (`juno=`)

```sh
# List the built-in JunoCam catalog
p.in.archive -l

# Import PJ1 (first science perijove, 2016-07-31) framelet sequence
p.in.archive juno=pj01_01c03606 output=junocam_pj01

# Import PJ7 (2017-07-04) framelet sequence
p.in.archive juno=pj07_07c00613 output=junocam_pj07
```

Detached-label PDS3 image (`.LBL` + `.IMG`), 16-bit unsigned DN, hosted
on the PDS Imaging Node (`planetarydata.jpl.nasa.gov`). Each file is a
full perijove pass: a multi-framelet stack of all 4 color-filter strips
(blue 420–520 nm, green 500–600 nm, red 590–710 nm, methane 880–900 nm)
interleaved by color, typically hundreds of bands total. Imported via
`p.in.pds3`; `output=` becomes a GRASS imagery group.

To extract a single color channel, select every 4th band starting from
1 (blue), 2 (green), 3 (red), or 4 (methane) and composite:
```sh
p.in.archive juno=pj01_01c03606 output=juno_pj01
# Extract the red framelets (every 4th band from band 3):
r.mapcalc "pj01_red_frame1 = juno_pj01.3"
```

## EXAMPLES

### List products matching a keyword

```sh
p.in.archive -l search="MOLA 64ppd" limit=5
```

### List the built-in USGS COG catalog

```sh
p.in.archive -l
```

### Window a global LOLA DEM to the active region

```sh
g.region n=337590 s=130740 e=263040 w=-8850 res=30
p.in.archive cog=moon_lola_dem_118m output=lola_sector
```

### Import a Moon LOLA DEM by DOI

```sh
p.in.archive doi=10.17189/1520642 output=lola_dem
```

### Import a specific PDS4 product by LID

```sh
p.in.archive \
    lid="urn:nasa:pds:mgs-mola-dem-mars:data:megt90n000cb" \
    output=mola_megt90
```

### Import a Mars Express OMEGA EDR cube (Mars, IR-VIS imaging spectrometer)

```sh
# Run in an XY GRASS project (raw instrument frame, not georeferenced)
p.in.archive omega=orb0100_0 output=omega_orbit100
# Output: imagery group omega_orbit100.1 .. omega_orbit100.352

# Import the scanning mirror sideplane needed for p.phocube -c:
p.in.pds3 input=~/RSDATA/Mars/ORB0100_0.QUB output=omega_mirror_dn suffix_band=1
```

### Import a Cassini VIMS cube (Titan, IR+VIS imaging spectrometer)

```sh
# List the curated VIMS catalog
p.in.archive -l

# Import the IR channel of a Titan T-108 flyby observation
p.in.archive vims=titan_v1799424623 vims_channel=ir output=vims_titan_ir
# Output: imagery group vims_titan_ir.1 .. vims_titan_ir.256

# Import the VIS channel of the same observation
p.in.archive vims=titan_v1799424623 vims_channel=vis output=vims_titan_vis
# Output: imagery group vims_titan_vis.1 .. vims_titan_vis.96

# Import an Enceladus VIMS cube and its companion VIS channel:
p.in.archive vims=enceladus_v1484504730 vims_channel=ir output=vims_enc_ir
```

## Saturn ring imaging pipelines

**p.in.archive** is Step 2 in both Cassini ring imaging chains — it
fetches the raw PDS3 image from OPUS so that no manual download is needed.
The full pipelines are described in [p.in.rings](p.in.rings.md); summaries
below show where this module fits.

### Chain A — SOI B-ring, radlong + RingCylindrical (analysis)

Image `N1467344155_2.IMG`, 2004-07-01T03:11:40, inner B ring, ≈86 400 km.
Full ready-to-run script: `$HOME/RSDATA/cassini_soi_b_ring.sh`.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS  ← this module ────────────────────────────────
p.in.archive opus_id=co-iss-n1467344155 output=N1467344155_raw

# ── Step 3: Set radlong output region ────────────────────────────────────────
g.region n=86550 s=86250 e=66.55 w=66.25 nsres=0.25 ewres=0.0003

# ── Step 4: Project raw image to ring-plane space (p.in.rings) ───────────────
KDIR="$HOME/RSDATA/Saturn/kernels"
p.in.rings \
    input=N1467344155_raw output=N1467344155_rings \
    time="2004-07-01T03:11:40" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=radlong filter="CL1/CL2" \
    kernels="${KDIR}/lsk/naif0012.tls,${KDIR}/sclk/cas00172.tsc,\
${KDIR}/ik/cas_iss_v10.ti,${KDIR}/fk/cas_v40.tf,\
${KDIR}/pck/cpck_rock_21Jan2011_merged.tpc,${KDIR}/pck/pck00010.tpc,\
${KDIR}/spk/040701AP_SCPSE_04173_04236.bsp,\
${KDIR}/ck/04183_04185ra.bc"

# ── Step 5: RingCylindrical projection (p.rings.project) ─────────────────────
p.rings.project \
    input=N1467344155_rings output=N1467344155_ringcyl \
    center_radius=86400 center_lon=66.39
r.colors map=N1467344155_ringcyl color=grey

# ── Step 6: Radial statistics (p.rings.stats) ─────────────────────────────────
p.rings.stats input=N1467344155_ringcyl \
    rmin=86250 rmax=86550 bin_width=5 \
    output=soi_bring_profile.csv radial=soi_bring_radial
```

### Chain B — outer A ring polar (display)

Image `N1467346624_2.IMG`, 2004-07-01T03:52:49, observer ring elevation +26.9°.
Shows the outer A ring (Keeler Gap region, 136 272–136 649 km) as an arc in
polar ring-plane coordinates.  Full ready-to-run script:
`$HOME/RSDATA/cassini_rev014_polar.sh`.

**Why `product=raw` is required here**: the CISSCAL 4.0beta calibrated product
(`_CALIB.IMG`) for this image uses -1.91×10³⁸ as a sentinel for invalid pixels
and flags nearly all ring pixels as invalid, leaving fewer than 20 valid output
pixels after projection.  The raw PDS3 image combined with the per-column
destripe in Step 3 is the correct path for this observation.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

KDIR="$HOME/RSDATA/Saturn/kernels"
IMAGE_MID_TIME="2004-07-01T03:52:49"
OPUS_ID="co-iss-n1467346624"
RAWMAP="N1467346624_polar_raw"
POLMAP="N1467346624_polar"

# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
SPICE_OUT=$(p.spice.find spacecraft=CASSINI time="${IMAGE_MID_TIME}" \
    dest="${KDIR}" 2>&1)
SPK_BASE=$(echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bsp' | tail -1)
CK_BASE=$( echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bc'  | tail -1)
LSK=$(find  "${KDIR}/lsk"  -name "*.tls"          | sort | tail -1)
SCLK=$(find "${KDIR}/sclk" -name "cas*.tsc"       | sort | tail -1)
IK=$(find   "${KDIR}/ik"   -name "cas_iss*.ti"    | sort | tail -1)
FK=$(find   "${KDIR}/fk"   -name "cas_v*.tf"      | sort | tail -1)
PCK1=$(find "${KDIR}/pck"  -name "cpck_rock*.tpc" | sort | tail -1)
PCK2=$(find "${KDIR}/pck"  -name "pck[0-9]*.tpc"  | sort | tail -1)
KERNELS="${LSK},${SCLK},${IK},${FK},${PCK1},${PCK2},\
${KDIR}/spk/${SPK_BASE},${KDIR}/ck/${CK_BASE}"

# ── Step 2: Fetch raw image from OPUS  ← this module ─────────────────────────
# product=raw bypasses the auto preference for the calibrated (_CALIB.IMG)
# product.  The companion .LBL label is matched by base name automatically.
# The file is downloaded to ~/RSDATA/Misc/ and imported via p.in.pds3, which
# reads LINE_PREFIX_BYTES=24 (ISS dark/overclocked pixel prefix) correctly.
p.in.archive opus_id="${OPUS_ID}" output="${RAWMAP}" \
    product=raw --overwrite
g.region raster="${RAWMAP}"

# ── Step 3: Per-column destripe ────────────────────────────────────────────────
# ISS NAC CCD column-to-column bias (~10–50 DN/column) projects as diagonal
# stripes at 26.9° ring elevation.  Column baseline is estimated from
# background pixels only (below global 70th percentile) to exclude ring signal.
python3 - "${RAWMAP}" << 'PYEOF'
import sys, tempfile, os
import numpy as np
import grass.script as gs

name = sys.argv[1]
reg  = gs.region()
nr, nc = int(reg["rows"]), int(reg["cols"])
tmp = tempfile.mktemp(suffix=".bin")
gs.run_command("r.out.bin", input=name, output=tmp,
               bytes=4, flags="f", null="-9999", quiet=True)
raw = np.fromfile(tmp, dtype=np.float32).reshape(nr, nc).astype(np.float64)
null_mask = (raw == -9999.0)
raw[null_mask] = np.nan
global_thresh = np.nanpercentile(raw, 70)
background    = np.where(raw < global_thresh, raw, np.nan)
col_bias      = np.nanmedian(background, axis=0)
all_bright    = np.isnan(col_bias)
if np.any(all_bright):
    col_bias[all_bright] = np.nanmedian(raw[:, all_bright], axis=0)
raw -= col_bias[np.newaxis, :]
raw[null_mask] = -9999.0
raw.astype(np.float32).tofile(tmp)
gs.run_command("r.in.bin", input=tmp, output=name, bytes=4, flags="f",
               north=reg["n"], south=reg["s"], east=reg["e"], west=reg["w"],
               rows=nr, cols=nc, anull="-9999", overwrite=True, quiet=True)
os.unlink(tmp)
PYEOF

# ── Step 4: Set polar ring-plane output region (km × km) ─────────────────────
# Image centre → r=136 504 km, lon=67.17° → x≈52 970 km, y≈125 808 km.
# 455 × 435 km box at 1 km/pixel captures the outer A ring arc.
g.region n=126024 s=125589 e=53198 w=52743 nsres=1 ewres=1

# ── Step 5: Project to polar ring-plane coordinates (p.in.rings) ─────────────
p.in.rings \
    input="${RAWMAP}" output="${POLMAP}" \
    time="${IMAGE_MID_TIME}" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KERNELS}" grid=9 --overwrite

# ── Step 6: Display ────────────────────────────────────────────────────────────
# Histogram-equalized grey: ring arc (~950 DN) vs background (~0 DN) requires
# non-linear mapping; -e ensures the arc is visually clear.
r.colors -e map="${POLMAP}" color=grey
d.mon start=wx0 && d.rast "${POLMAP}"
```

## REFERENCES

- USGS Astropedia STAC API: <https://stac.astrogeology.usgs.gov/api/>
- NASA PDS Federated Search API: <https://pds.nasa.gov/api/search/1/>
- OPUS Ring-Moon Systems Node API: <https://opus.pds-rings.seti.org/api/>
- PDS Geosciences Node, MRO/CRISM TRDR archive: <https://pds-geosciences.wustl.edu/mro/mro-m-crism-3-rdr-targeted-v1/>
- STAC specification: <https://stacspec.org/>
- PDS4 Information Model: <https://pds.nasa.gov/datastandards/documents/>

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF SPICE kernels
- [p.in.rings](p.in.rings.md) — project raw camera image to ring-plane space
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.rings.stats](p.rings.stats.md) — radial brightness statistics
- [p.in.pds4](p.in.pds4.md), [p.in.pds3](p.in.pds3.md), [p.in.isis](p.in.isis.md)

## AUTHOR

Yann Chemin (dr.yann.chemin@gmail.com)

## LICENSE

The Unlicense ([https://unlicense.org](https://unlicense.org)) —
this module is released into the public domain.
