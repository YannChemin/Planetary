## DESCRIPTION

*p.stereo* produces a topographic DEM from a pair of overlapping planetary
images using the [NASA Ames Stereo Pipeline (ASP)](https://github.com/NeoGeographyToolkit/StereoPipeline).
It wraps the ASP `stereo` + `point2dem` pipeline, supplying the correct
planetary ellipsoid parameters from the bodies/ JSON library, and imports
the resulting DEM into the current GRASS mapset.

ASP must be installed separately and its binaries (`stereo`, `point2dem`)
must be in the PATH or a standard location (see NOTES).

## PIPELINE

Three steps in sequence:

1. **bundle_adjust** (optional, flag `-b`) — refines relative camera orientation
   to reduce residual pointing errors before correlation. Recommended for HiRISE
   and CaSSIS pairs.
2. **stereo** — full stereo correlation producing a dense disparity map and
   point cloud (`run-PC.tif`). Algorithm and alignment method configurable.
3. **point2dem** — converts the point cloud to a raster DEM using the body
   semi-major and semi-minor axes. Output GeoTIFF imported into GRASS via
   `r.import`.

The optional `-o` flag additionally runs `mapproject` on both input images,
projecting them onto the DEM and importing as `<output>_left` and `<output>_right`.

## INPUT IMAGES

Input images must be either:
- ISIS cubes (`.cub`) run through `spiceinit` (via *p.spiceinit* or ISIS3 directly)
- PDS raw images (`.img`) with matching label files — ASP reads these directly
- GeoTIFFs with RPC or map-projected coordinates

For HiRISE, HRSC, and CaSSIS stereo, use ISIS `.cub` files.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `left=` | required | Left/reference input image path |
| `right=` | required | Right/match input image path |
| `output=` | required | Output GRASS DEM raster name |
| `body=` | required | Planetary body (mars, moon, mercury, venus, titan, europa, enceladus, ceres) |
| `workdir=` | `$HOME/RSDATA/<body>/stereo/<name>/` | Working directory for ASP intermediate files |
| `alignment=` | affineepipolar | Stereo alignment method |
| `algorithm=` | asp_mgm | Stereo correlation algorithm |
| `spacing=` | (auto) | Output DEM resolution in metres |
| `stereo_opts=` | (none) | Extra options passed verbatim to stereo |
| `point2dem_opts=` | (none) | Extra options passed verbatim to point2dem |

## FLAGS

| Flag | Effect |
|---|---|
| `-b` | Run bundle_adjust before stereo (improves alignment) |
| `-k` | Keep intermediate ASP files after import |
| `-o` | Orthorectify both images onto the DEM (mapproject) |

## ALGORITHMS

| Algorithm | Notes |
|---|---|
| `asp_mgm` | More Global Matching — best quality for orbital imagery (default) |
| `asp_sgm` | Semi-Global Matching — good balance of speed/quality |
| `asp_bm` | Block matching — fastest, least accurate |
| `asp_final_mgm` | MGM with final refinement pass |
| `msmw` / `msmw2` | Multi-scale — handles large radiometric differences |

## NOTES

- ASP is not included in the Planetary GRASS add-ons package. Download a binary
  release from [github.com/NeoGeographyToolkit/StereoPipeline](https://github.com/NeoGeographyToolkit/StereoPipeline/releases)
  and place `bin/` in your PATH.
- The module searches for ASP binaries in PATH and in `~/asp/bin`,
  `~/StereoPipeline/bin`, `/usr/local/asp/bin`, `/opt/asp/bin`.
- Stereo processing can require tens to hundreds of GB of temporary disk space.
  Use `workdir=` to direct intermediate files to a volume with sufficient space.
- Working directory structure:
  ```
  $workdir/stereo/    ← stereo correlation output (run-PC.tif, etc.)
  $workdir/ba/        ← bundle adjustment output (if -b)
  $workdir/dem/       ← point2dem output DEM GeoTIFF
  $workdir/ortho_*/   ← mapproject output (if -o)
  ```
- Pass additional ASP flags via `stereo_opts=` and `point2dem_opts=`.
- For the Moon: `point2dem_opts="--datum D_MOON"` uses the LOLA areoid.

## EXAMPLE

```sh
# HiRISE stereo pair already spiceinit'd as ISIS cubes

p.stereo \
    left=ESP_011531_1755_RED.cub \
    right=ESP_019853_1755_RED.cub \
    output=hirise_dem \
    body=mars \
    alignment=affineepipolar \
    algorithm=asp_mgm \
    workdir=$HOME/RSDATA/Mars/stereo/mawrth \
    -b -o

r.colors map=hirise_dem color=srtm
d.shade shade=hirise_dem color=hirise_dem_left brighten=40
```

## REFERENCES

Beyer, R.A., Alexandrov, O., & McMichael, S. (2018).
"The Ames Stereo Pipeline: NASA's open source software for deriving and
processing terrain data." *Earth and Space Science*, 5, 537–548.

Moratto, Z., Broxton, M., Beyer, R.A., Lundy, M., & Husmann, K. (2010).
"Ames Stereo Pipeline, NASA's Open Source Automated Stereogrammetry Software."
*Lunar and Planetary Science Conference*, 41, abstract 2364.

## SEE ALSO

*p.in.archive*, *p.spiceinit*, *p.cam2map*, *p.photoclinometry*, *r.import*

## AUTHOR

Yann Chemin
