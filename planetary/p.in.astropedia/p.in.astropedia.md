## DESCRIPTION

*p.in.astropedia* searches for and imports planetary data products from
two public registries:

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

On download, the file format is detected by extension and dispatched
to the appropriate GRASS importer:

| Extension  | Importer     |
|------------|--------------|
| `.tif`/`.tiff` | `r.in.gdal` |
| `.fits`    | `r.in.gdal`  |
| `.img`     | `p.in.pds3`  |
| `.xml`     | `p.in.pds4`  |

## NOTES

### Spatial pre-filtering from the active GRASS region

On startup *p.in.astropedia* reads the current GRASS computational
region (equivalent to `g.region -p`) and extracts the longitude/latitude
bounding box [W, S, E, N]. This bbox is forwarded to both the STAC
`bbox` filter and the PDS API `bbox` parameter so that **only products
whose footprint intersects the active map window are returned**.

This works transparently for planetary body Locations: GRASS always
stores region extents in geographic degrees, so the filter is valid even
for non-Earth datums (Mars, Moon, Mercury …).

Use the **`-r` flag** to disable the spatial pre-filter and search globally.

```sh
# List Moon DEM products that overlap the active map window
p.in.astropedia -l search="LOLA DEM"

# Same search without spatial constraint
p.in.astropedia -lr search="LOLA DEM"
```

- An internet connection is required. The module makes HTTPS requests
  to `stac.astrogeology.usgs.gov` and/or `pds.nasa.gov`.
- Downloaded files are placed in **`download_dir=`** (default: system
  temp dir) and deleted after a successful import unless **`-k`** is
  given or **`download_dir=`** is set explicitly.
- The `-l` flag lists matching products and exits without downloading,
  useful for browsing before committing to a large download.
- When `doi=` is given, the DOI is resolved via `doi.org` and the
  resulting landing URL is used as a keyword against the Astropedia
  STAC. If no STAC match is found, the NASA PDS API is tried next.
- When `lid=` is given, the NASA PDS API is queried first (exact LID
  match), then Astropedia STAC as fallback.
- Only the **first** result is downloaded automatically. Use `-l` to
  inspect all candidates, then re-run with the exact `lid=` or item `id=`
  of the desired product.
- Multi-band products: use `band=` to select a specific band (default 1).
  To import all bands, use *r.in.gdal* directly on the cached file.
- The `-o` flag is passed to the underlying importer to bypass
  projection mismatch errors (use with caution).

### USGS COG mosaics (`cog=`)

- The COG catalog is built in; list it with **`-l`** (no other source needed).
- `cog=` accepts either a catalog key or a direct HTTPS URL.
- `cog=` cannot be combined with `doi=`/`lid=`/`search=`.

#### Local cache and `wget -c` pre-download (since v0.8.5)

Remote COGs (http/https) are pre-downloaded with `wget -c` (resumable,
5 retries, 60-second timeout) into a per-body local cache before
`r.import` is invoked on the local copy. This eliminates the transient
`/vsicurl/` chunk-read failures that previously bit large HiRISE / PDS
S3 tiles (`TIFFReadEncodedTile` errors at random row offsets). Cached
files are reused across runs whenever the local size matches the
remote `Content-Length`.

Body inference for the cache path (since v0.8.7) checks the URL path
segments first (`…/mars/…` → `Mars`) and, if no segment matches, falls
back to scanning the filename basename (`Ceres_Dawn_FC_HAMO_…tif` →
`Ceres`; `Lunar_LRO_LOLA_…tif` → `Moon`). The cache directory is
`~/RSDATA/<Body>/`.

#### Auto-project + region alignment (since v0.8.6)

Pass `project=<NAME>` to make the import body-CRS-faithful end to end:

1. The source's native CRS is read with `gdalsrsinfo` (works for both
   local files and `/vsicurl/` remote COGs).
2. A fresh GRASS project of that name is created via `g.proj -c wkt=…`
   (or an existing project of the same name is reused). The current
   GRASS session is switched into that project.
3. `r.import` is invoked with `-o` so cosmetic WKT differences
   (e.g. "Mars (2015) - Sphere / Ocentric / Equirectangular" versus
   "Equirectangular Mars") do not trip the strict WKT comparator.
4. After import, `g.region raster=<output> -s` aligns the active
   region to the imported raster's extent and resolution, and saves
   that as the project's `DEFAULT_WIND`. The project is therefore
   usable out of the box without an additional `g.region` call —
   subsequent `r.what`, `r.info`, `p.landing`, etc. operate on the
   full raster by default. The original project is restored at exit.

This is the workflow used throughout the multi-body chapter of the
Planetary Landing Modeling article (Mars Jezero HiRISE, Europa Pwyll
Galileo SSI, Ceres Occator HAMO, Enceladus SPT Bland 2019 — each one
ingest plus auto-create-project in a single command).

## EXAMPLES

### List products matching a keyword

```sh
p.in.astropedia -l search="MOLA 64ppd" limit=5
```

### List the built-in USGS COG catalog

```sh
p.in.astropedia -l
```

### Window a global LOLA DEM to the active region (no full download)

```sh
g.region n=337590 s=130740 e=263040 w=-8850 res=30   # article sector
p.in.astropedia cog=moon_lola_dem_118m output=lola_sector
```

### Import any USGS COG by direct URL

```sh
p.in.astropedia \
    cog=https://planetarymaps.usgs.gov/mosaic/Mars_MGS_MOLA_DEM_mosaic_global_463m.tif \
    output=mola_sector
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

### Browse Astropedia, keep the downloaded file

```sh
p.in.astropedia -lk \
    search="CTX mosaic mars" \
    download_dir=/data/planetary
```

### ISIS3-equivalent workflow

ISIS3's `dawnfc2isis` / `mrf2isis` / `kaguyami2isis` each handle one
sensor format. The Astropedia import approach in GRASS replaces the
need to know the per-mission converter:

```sh
# ISIS3 workflow for a PDS4-distributed CTX product
pds2isis from=J03_045820_1986.xml to=ctx_scene.cub

# GRASS equivalent — resolves PDS4 label automatically
p.in.astropedia lid="urn:nasa:pds:mro_ctx:data:j03_045820_1986" \
    output=ctx_scene
```

## REFERENCES

- USGS Astropedia STAC API: <https://stac.astrogeology.usgs.gov/api/>
- NASA PDS Federated Search API: <https://pds.nasa.gov/api/search/1/>
- STAC specification: <https://stacspec.org/>
- PDS4 Information Model: <https://pds.nasa.gov/datastandards/documents/>

## SEE ALSO

*[p.in.pds4](p.in.pds4.md),
[p.in.pds3](p.in.pds3.md),
[p.in.isis](p.in.isis.md)*

## AUTHOR

Yann Chemin (dr.yann.chemin@gmail.com)

## LICENSE

The Unlicense ([https://unlicense.org](https://unlicense.org)) —
this module is released into the public domain.
