## DESCRIPTION

`p.in.lroc.nac` lists and imports **LROC NAC DTMs** (LRO Narrow-Angle
Camera stereo-derived Digital Terrain Models) from the ASU LROC PDS
archive at *pds.lroc.asu.edu*. NAC DTMs are site-specific GeoTIFFs at
~2–5 m posting; the archive holds 600+ products covering Apollo sites,
Constellation/Artemis candidates, crater rims, ridges, and other
features of scientific or operational interest.

The module:

1. scrapes the archive directory listing (one HTTP request, cached);
2. for each product, fetches the PDS4 `.xml` label and parses its
   `cart:Bounding_Coordinates` to build a local lat/lon index;
3. filters by bounding box and/or product-name substring;
4. downloads the matching product's `.TIF`, `.LBL`, and `.xml` into a
   cache directory;
5. imports the GeoTIFF via *r.in.gdal*.

## PARAMETERS

- **bbox**=*W,S,E,N* — Bounding box in degrees, east-positive longitudes
  in [0,360]. Polar studies typically use a full-longitude window such
  as `0,-90,360,-83`. Dateline-crossing (W > E) is supported.
- **name**=*substring* — Case-insensitive substring matched against the
  product name (e.g. `NOBILE` matches `NOBILE01`, `NOBILE02`, …).
  Combined with `bbox` by AND.
- **output**=*name* — GRASS raster name for the imported DTM. Required
  unless `-l` is given. The bbox/name filters must select exactly one
  product when `output` is set.
- **download_dir**=*path* — Default
  `$HOME/.cache/p_in_lroc_nac/data`.
- **cache_index**=*path* — Default
  `$HOME/.cache/p_in_lroc_nac/index.json`. Built on first run; rebuilt
  when `-r` is passed.
- **limit**=*n* — Default 20.
- **workers**=*n* — Default 16.

## FLAGS

- **-l** — List matching products and exit.
- **-r** — Refresh: re-scrape the archive and rebuild the local index.
- **-k** — Keep downloaded files after import.
- **-d** — Download-only: cache matching products in `download_dir` and skip GRASS import. Useful when staging files on a workstation for later rsync to a compute server, or when the active GRASS project is in a different CRS than the NAC DTM and you intend to import later with *r.proj*. Mutually exclusive with `output`. Accepts multiple matches.
- **-o** — Pass `-o` to `r.in.gdal` to override CRS mismatch.

## EXAMPLES

```sh
# Build the index (one-shot)
p.in.lroc.nac -r

# List south-polar candidates
p.in.lroc.nac bbox=0,-90,360,-80 -l limit=50

# List Nobile-area DTMs
p.in.lroc.nac name=NOBILE -l

# Fetch + import a specific product
p.in.lroc.nac name=NOBILE01 output=nac_nobile01_dtm -k

# Download-only: cache files under $HOME/RSDATA/Moon/NAC for later sync
# (keeps NAC DTMs alongside the LOLA polar caps under one canonical tree)
p.in.lroc.nac name=MALAPERT -d download_dir=$HOME/RSDATA/Moon/NAC
rsync -av $HOME/RSDATA/Moon/NAC/  user@server:RSDATA/Moon/NAC/
```

### Worked example: Artemis-III candidate-site coverage

For the 9-region Artemis-III evaluation handled by *p.landing*, the
south-polar NAC DTM index lists 12 products with lat < -80°.
Intersecting those with the 9 candidate centroids yields NAC coverage
for only 2 sites (`connecting_ridge` → `ESALL_CR1`; `malapert_massif` →
`MALAPERT02` / `MALAPERT03`), plus 1 useful sensitivity-analysis
product (`ESALL_SR12`, Shackleton rim). The other 7 sites fall back to
LOLA 5 m / 20 m / 30 m depending on latitude.

```sh
# Cache the 4 Artemis-relevant NAC DTMs (~120 MB total) into the
# standard $HOME/RSDATA/Moon/ tree, alongside the LOLA polar caps:
NAC=$HOME/RSDATA/Moon/NAC
p.in.lroc.nac name=ESALL_CR1   -d download_dir=$NAC
p.in.lroc.nac name=ESALL_SR12  -d download_dir=$NAC
p.in.lroc.nac name=MALAPERT02  -d download_dir=$NAC
p.in.lroc.nac name=MALAPERT03  -d download_dir=$NAC

# Sync to compute server (mirroring the same path)
rsync -av $NAC/  user@server:RSDATA/Moon/NAC/

# On the server, in a polar-stereographic GRASS project:
r.in.gdal input=$HOME/RSDATA/Moon/NAC/ESALL_CR1/NAC_DTM_ESALL_CR1.TIF \
          output=nac_esall_cr1_dtm
```

Pattern: **discover with `-l`, cache with `-d`, sync, import on the
target box** — avoids running the pipeline on a workstation just to
land bytes in the right place.

## NOTES

- The canonical `pds.lroc.asu.edu` 301-redirects to the
  `pds.lroc.im-ldi.com` mirror; the module talks to the mirror directly.
- NAC DTM footprints are narrow (~5×25 km typical); a 15×15 km study
  box may straddle several products or fall outside any. Use `-l` first
  to confirm coverage.
- DTMs are published in product-native projections (commonly
  south-polar stereographic on the IAU lunar sphere R=1737400). The
  imported raster carries the source projection; reproject with
  *r.proj* if needed.

## SEE ALSO

*[p.in.astropedia](p.in.astropedia.md),
[p.in.pds3](p.in.pds3.md),
[p.in.pds4](p.in.pds4.md),
[r.in.gdal](https://grass.osgeo.org/grass-stable/manuals/r.in.gdal.html)*

## REFERENCES

- LROC SOC, ASU. NAC DTM archive:
  `https://pds.lroc.asu.edu/data/LRO-L-LROC-5-RDR-V1.0/`
- Henriksen, M.R., et al. *Extracting accurate and precise topography
  from LROC narrow angle camera stereo observations*, Icarus, 2017.

## AUTHOR

Yann Chemin, *dr.yann.chemin@gmail.com*
