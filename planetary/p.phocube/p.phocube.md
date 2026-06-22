## DESCRIPTION

*p.phocube* computes per-pixel photometric/geometric backplane rasters
(incidence, emission, phase, latitude, longitude, local radius, pixel
resolution) for a planetary image, against an ellipsoid shape model. For
each output band selected by flags, a separate GRASS raster is written
named **output_bandname**.

Available backplane bands:

| Flag | Band name | Description |
|---|---|---|
| -i | incidence | Solar incidence angle at surface (degrees) |
| -e | emission | Emission angle to sensor (degrees) |
| -p | phase | Phase angle Sun-surface-sensor (degrees) |
| -t | lat | Planetocentric latitude (degrees) |
| -n | lon | Longitude (degrees) |
| -r | local_radius | Local ellipsoid radius (km) |
| -x | resolution | Approximate ground-sample distance (km/pixel) |
| -a | (all of the above) | |

Two operating modes:

1. **Flat-field mode (default)**: the user supplies fixed solar-direction
   (`sun_x/y/z=`) and observer-position (`obs_x/y/z=`) vectors in
   body-fixed coordinates, applied uniformly to every pixel. Each pixel's
   own (lat, lon) is derived directly from the GRASS region's east/north
   — **the active region must already be the scene's real geographic
   footprint** (degrees), not an un-georeferenced sample/line pixel grid
   (see NOTES).
2. **SPICE mode (`-s`)**: real per-pixel ephemeris geometry. Reads
   `target=`/`observer=`/`time=` and kernel paths from the input map's
   history (written by *p.spiceinit*), converts each pixel's region
   east/north to real (lon, lat) — directly if the location is already
   geographic, or via `GPJ_transform()` if it's a projected CRS — builds
   the body-fixed surface point at that (lat, lon) on the body's real
   ellipsoid (radii taken from the loaded PCK, not the `a_radius=`/
   `b_radius=`/`c_radius=` CLI defaults), and calls NAIF CSPICE's
   `ilumin` once per pixel for the real incidence/emission/phase angles
   at the given observation time. Fails with `G_fatal_error` if the
   active region is an un-georeferenced pixel/line grid
   (`PROJECTION_XY`) — see NOTES.

## NOTES

### Flat-field mode and the region-as-lat/lon assumption

In flat-field mode (no `-s`), `p.phocube` treats the GRASS region's
east/north directly as longitude/latitude with **no projection
awareness** — this is a deliberate, documented simplification, not a
bug, but it means the active region must be set to the scene's real
ground footprint (e.g. from the product's own corner coordinates) before
running it. For raw sensor-grid products (e.g. CRISM TRDR cubes
imported pixel/line via `p.in.pds3 -g`), set a temporary geographic
region matching the real footprint, run `p.phocube`, then copy the
resulting backplanes back onto the cube's native pixel/line region (a
row/col-shaped array round-trip via `r.out.bin`/`r.in.bin`, not a
reprojection, since both regions share the same row/col count) — see
`p.atcorr.md`'s worked Mars example for the full sequence.

### SPICE mode (`-s`): scope and requirements

- Requires `target=`/`observer=`/`time=` and at least one kernel to have
  been attached via `p.spiceinit` beforehand. Missing any of these is a
  `G_fatal_error`, not a silent fallback.
- Requires the active region to be a real geographic or projected CRS.
  An un-georeferenced pixel/line grid (`PROJECTION_XY`, as produced by
  `p.in.pds3 -g` for raw, un-projected pushbroom/framing cubes such as
  CRISM TRDR) is rejected with `G_fatal_error` — there is no real camera
  model (instrument boresight/FOV + per-scan-line CK timing) anywhere in
  this suite to compute genuine per-pixel look-direction rays for that
  case, and silently treating sample/line indices as degrees would be
  the wrong answer, not an approximation. Re-project the product to its
  real footprint first (most RDR/COG products, e.g. HiRISE/CTX/MTRDR,
  already are georeferenced and work directly), or use flat-field mode.
- Uses one mid-observation epoch (`time=`) for the whole scene — not
  per-line/per-pixel timing. This is an approximation, adequate for
  framing/pushbroom scenes short enough that spacecraft motion during
  acquisition doesn't materially change the geometry; it is not a real
  per-scanline camera model.
- Still uses the existing ellipsoid-only shape model (no DSK/real-shape
  intercepts) — `-s` changes *which geometry engine drives the
  ellipsoid* (real ephemeris vs. fixed flat-field vectors), not the
  shape model itself.
- A generic planetary ephemeris SPK (e.g. `de430.bsp`, `de440s.bsp`)
  typically carries each *system barycenter* (e.g. MARS BARYCENTER, ID
  4) but not necessarily the individual planet body itself (e.g. MARS,
  ID 499) for bodies whose moons barely perturb the system barycenter.
  `p_spice_ilumin` needs the actual target body's state, so `-s` mode
  requires a kernel set that actually carries that body (e.g. a
  body/mission-specific SPK, as real mission meta-kernels always
  include) — a generic-only planetary ephemeris can produce a CSPICE
  `SPKINSUFFDATA` error for some bodies even though the file loads fine.

### Validated against real data

Confirmed working end-to-end against real NAIF kernels (LSK + PCK +
`de430.bsp`) and a real `p.spiceinit`-attached target/observer/time, in a
real geographic GRASS location: read-history → kernel load → ephemeris
time conversion → real PCK radii → per-pixel `ilumin` all produced sane,
smoothly per-pixel-varying, non-NULL incidence/emission/phase. The
`PROJECTION_XY` guard was confirmed to reject an un-georeferenced
pixel/line input with `G_fatal_error` rather than silently misinterpreting
sample/line indices as degrees. Both are covered by regression tests in
`testsuite/test_pphocube.py`.

## EXAMPLES

Flat-field mode, region already set to the scene's real geographic
footprint:

```sh
g.region n=22.406 s=22.272 e=-17.946 w=-18.433 rows=15 cols=64
p.phocube input=crism_mawrth_ir.1 target=mars \
    sun_x=0.55 sun_y=-0.10 sun_z=0.82 \
    obs_x=3254.8 obs_y=-1057.5 obs_z=1406.4 \
    -iep output=mawrth_geom
```

SPICE mode, on an already-georeferenced product (e.g. a HiRISE RDR/COG):

```sh
p.spiceinit map=hirise_red target=MARS observer=MRO \
    time=2007-01-05T01:26:56 \
    lsk=naif0012.tls pck=pck00011.tpc spk=mro_struct.bsp ck=mro_sc.bc
p.phocube -s -iep input=hirise_red output=hirise_geom
# produces: hirise_geom_incidence, hirise_geom_emission, hirise_geom_phase
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65-70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

- Hapke, B. (1981). Bidirectional reflectance spectroscopy 1. Theory.
  *J. Geophys. Res.* 86(B4):3039-3054.
  doi:[10.1029/JB086iB04p03039](https://doi.org/10.1029/JB086iB04p03039)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.atcorr](p.atcorr.md),
[p.atcorr.hapke](p.atcorr.hapke.md)*

## AUTHOR

Yann Chemin
