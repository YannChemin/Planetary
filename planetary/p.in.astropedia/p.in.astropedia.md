# p.in.astropedia - Fetch and import planetary data from USGS Astropedia, NASA PDS, or OPUS

## DESCRIPTION

*p.in.astropedia* searches for and imports planetary data products from
three public registries:

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

### Spatial pre-filtering from the active GRASS region

On startup *p.in.astropedia* reads the current GRASS computational
region (equivalent to `g.region -p`) and extracts the longitude/latitude
bounding box [W, S, E, N]. This bbox is forwarded to both the STAC
`bbox` filter and the PDS API `bbox` parameter so that **only products
whose footprint intersects the active map window are returned**.

Use the **`-r` flag** to disable the spatial pre-filter and search globally.

```sh
# List Moon DEM products that overlap the active map window
p.in.astropedia -l search="LOLA DEM"

# Same search without spatial constraint
p.in.astropedia -lr search="LOLA DEM"
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
p.in.astropedia -l opus="instrument=Cassini VIMS,target=Saturn" output=x

# Import VIS channel of the first result
p.in.astropedia opus="instrument=Cassini VIMS,target=Titan" \
    vims_channel=vis output=vims_titan_vis

# Import a specific VIMS observation by OPUS ID (IR channel)
p.in.astropedia opus_id=co-vims-v1590123456 vims_channel=ir output=vims_ir
```

VIMS cubes are PDS3 format; both `.qub` channels (VIS: 96 bands,
0.35–1.05 µm; IR: 256 bands, 0.88–5.1 µm) are imported via `p.in.pds3`.
The companion `.lbl` label file is fetched automatically.

### MRO/CRISM TRDR products (`crism=`)

```sh
# List the built-in CRISM catalog
p.in.astropedia -l

# Import the Mawrth Vallis FRT00003BFB VNIR cube (S detector, 107 bands)
p.in.astropedia crism=mawrth_vallis_frt00003bfb_vnir output=crism_mawrth_vnir

# Import the IR cube (L detector, 438 bands, 1.00-3.92 um)
p.in.astropedia crism=mawrth_vallis_frt00003bfb_ir output=crism_mawrth_ir

# Or pass a direct https URL to any other TRDR .IMG on the archive
p.in.astropedia \
    crism="https://pds-geosciences.wustl.edu/mro/mro-m-crism-3-rdr-targeted-v1/mrocr_2101/trdr/2007/2007_005/FRT00003BFB/FRT00003BFB_01_IF156L_TRR3.IMG" \
    output=crism_mawrth_ir
```

Both catalog entries were verified live end-to-end (HTTP 200 on `.IMG`
and `.LBL`, successful `p.in.pds3 -g` import). `output=` becomes a GRASS
imagery group (`output.1` .. `output.N`, one raster per spectral band),
mirroring `p.in.pds3 -g`'s own convention; the region is aligned to
`output.1` after import. Downloads land in `~/RSDATA/Mars/` and are
resumable via `wget -c`, same as the OPUS path.

CRISM detector naming: **L** = long-wavelength/IR detector (1.00–3.92 µm,
438 bands); **S** = short-wavelength/VNIR detector (0.36–1.05 µm,
typically 107–184 bands depending on the segment's spectral binning).
A given FRT (Full Resolution Targeted) observation ID can have multiple
repeat segments (`_01_`, `_02_`, `_03_`, ...) acquired during the same
multi-pass campaign; the wavelength-filter code (e.g. `156`, `166`)
varies per segment and cannot be derived from the observation ID alone —
it was resolved here by greping the volume's
`collection_data_trdr_inventory.csv` and the monthly `index/trdrMMYY_index.tab`
table (column `FILE_SPECIFICATION_NAME`) on the real archive.

Downloads land in `~/RSDATA/<Body>/` and are resumable via `wget -c`.

## EXAMPLES

### List products matching a keyword

```sh
p.in.astropedia -l search="MOLA 64ppd" limit=5
```

### List the built-in USGS COG catalog

```sh
p.in.astropedia -l
```

### Window a global LOLA DEM to the active region

```sh
g.region n=337590 s=130740 e=263040 w=-8850 res=30
p.in.astropedia cog=moon_lola_dem_118m output=lola_sector
```

### Import a Moon LOLA DEM by DOI

```sh
p.in.astropedia doi=10.17189/1520642 output=lola_dem
```

### Import a specific PDS4 product by LID

```sh
p.in.astropedia \
    lid="urn:nasa:pds:mgs-mola-dem-mars:data:megt90n000cb" \
    output=mola_megt90
```

## Saturn ring imaging pipelines

**p.in.astropedia** is Step 2 in both Cassini ring imaging chains — it
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
p.in.astropedia opus_id=co-iss-n1467344155 output=N1467344155_raw

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
p.in.astropedia opus_id="${OPUS_ID}" output="${RAWMAP}" \
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
