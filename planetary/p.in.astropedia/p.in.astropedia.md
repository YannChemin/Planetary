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

### Chain B — A ring / F ring, polar (display)

Image `N1498508609_1.IMG`, 2005-06-26T19:55:52, observer ring elevation +38.7°.
Full ready-to-run script: `$HOME/RSDATA/cassini_rev014_polar.sh`.

```bash
# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2005-06-26T19:55:52" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS  ← this module ────────────────────────────────
p.in.astropedia opus_id=co-iss-n1498508609 output=N1498508609_raw

# ── Step 3: Set polar output region in km × km ───────────────────────────────
g.region n=130000 s=70000 e=-50000 w=-140000 nsres=50 ewres=50

# ── Step 4: Project to polar ring-plane coordinates (p.in.rings) ─────────────
KDIR="$HOME/RSDATA/Saturn/kernels"
p.in.rings \
    input=N1498508609_raw output=N1498508609_polar \
    time="2005-06-26T19:55:52" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KDIR}/lsk/naif0012.tls,${KDIR}/sclk/cas00172.tsc,\
${KDIR}/ik/cas_iss_v10.ti,${KDIR}/fk/cas_v40.tf,\
${KDIR}/pck/cpck_rock_21Jan2011_merged.tpc,${KDIR}/pck/pck00010.tpc,\
${KDIR}/spk/050824R_SCPSE_05217_05257.bsp,\
${KDIR}/ck/05289_05294ra.bc"
r.colors map=N1498508609_polar color=grey
d.rast N1498508609_polar
```

## REFERENCES

- USGS Astropedia STAC API: <https://stac.astrogeology.usgs.gov/api/>
- NASA PDS Federated Search API: <https://pds.nasa.gov/api/search/1/>
- OPUS Ring-Moon Systems Node API: <https://opus.pds-rings.seti.org/api/>
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
