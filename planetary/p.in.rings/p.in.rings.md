# p.in.rings - Project a raw camera image to ring-plane coordinates using SPICE

## DESCRIPTION

**p.in.rings** reprojects a raw spacecraft camera image (in pixel/sample
coordinates) into ring-plane coordinates using NAIF SPICE kernels.

Two output projection modes are available via the `projection=` parameter:

### radlong (default)

The GRASS region axes encode:
- **north/south = ring_radius [km]**
- **east/west = ring_longitude [degrees]**

Useful for radial/azimuthal analysis (statistics by radius or longitude
bin), but the mixed km/degree units mean the map cannot be displayed with
a correct aspect ratio — it will appear distorted unless you explicitly
account for `arc_length = r · Δlon · π/180`.

### polar

The GRASS region axes encode:
- **east/west = x [km]** in the ring plane (IAU body-fixed, longitude=0 → +x)
- **north/south = y [km]** in the ring plane (IAU body-fixed, longitude=90° → +y)

Both axes in km; the map is isotropic. Saturn's center is at the origin.
The view is from Saturn's **north pole**, following IAU convention.
The direction of the spacecraft (above or below the ring plane) is handled
automatically by the sign of the spacecraft's z-coordinate in the body-fixed
frame — no manual mirroring is needed.

Use this mode for cartographic display: the rings appear as arcs of circles
centred on Saturn, with correct geometry and a 1:1 aspect ratio.

**Ring-plane geometry**: the ring plane is the body's equatorial plane
(z = 0 in the body-fixed frame, e.g. `IAU_SATURN` for Saturn's rings).
For each sample point on a grid across the image, the module:
1. Constructs the pixel look-vector in the instrument frame
2. Rotates it to the body-fixed frame using the CK pointing kernel
3. Intersects the ray with z = 0 using the spacecraft position (SPK)
4. Records (ring_radius, ring_lon) at the intersection point

A bilinear model is fit over the sampling grid (residuals typically
< 0.1 km in ring_radius, < 0.0002° in ring_lon for a 1024-pixel image),
then inverted analytically to find each output pixel's source camera
coordinate.

## USAGE

```
p.in.rings input=raw_image output=rings_image \
    time=<UTC> instrument=<NAIF_id> \
    [spacecraft=CASSINI] [body=SATURN] [frame=IAU_SATURN] \
    [kernels=k1,k2,...] [grid=9] [projection=radlong|polar] \
    [filter=<FILTER_NAME>] [-n]
```

| Parameter | Description |
|---|---|
| `input` | Raw raster in pixel/sample coordinates (W=0 E=ncols S=0 N=nlines) |
| `output` | Output raster in ring-plane coordinates |
| `time` | Image mid-time, UTC ISO-8601 |
| `instrument` | NAIF instrument ID (e.g. `-82360` for Cassini ISS NAC) |
| `spacecraft` | NAIF spacecraft name (default `CASSINI`) |
| `body` | Central body (default `SATURN`) |
| `frame` | Body-fixed frame (default `IAU_SATURN`) |
| `kernels` | Comma-separated kernel paths; if omitted, loads from mapset `spice/` |
| `grid` | Geometry sampling grid size N×N (default 9) |
| `projection` | `radlong` (default) or `polar` — see DESCRIPTION |
| `filter` | Filter name from PDS3 label `FILTER_NAME` keyword (e.g. `CL1/CL2`, `RED/GRN`). Stored verbatim in `planetary.json` under `extended_metadata.planetary.filter_name`. |
| `-n` | Nearest-neighbour sampling instead of bilinear |

## Saturn ring imaging pipelines

Both complete chains start from the same location setup and SPICE kernel
download, then diverge at the region step.  Ready-to-run scripts live at
`$HOME/RSDATA/cassini_soi_b_ring.sh` (Chain A) and
`$HOME/RSDATA/cassini_rev014_polar.sh` (Chain B).

### Chain A — SOI B-ring, radlong + RingCylindrical (analysis)

Image `N1467344155_2.IMG`, 2004-07-01T03:11:40, inner B ring, 86 283–86 516 km.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS (p.in.astropedia) ────────────────────────────
p.in.astropedia opus_id=co-iss-n1467344155 output=N1467344155_raw

# ── Step 3: Set radlong output region ────────────────────────────────────────
# north/south = ring_radius [km],  east/west = ring_longitude [deg]
g.region n=86550 s=86250 e=66.55 w=66.25 nsres=0.25 ewres=0.0003

# ── Step 4: Project raw image to ring-plane space  ← this module ─────────────
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

### Chain B — Rev 014 B/A ring, polar (display)

Image `N1508963064_2.IMG`, 2005-10-23T14:17:00, sub-SC lat ≈+10°.
At this elevation the B and A rings appear as visible arcs; the polar
projection maps them as circles centred on Saturn with a correct 1:1
aspect ratio.

```bash
# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2005-10-23T14:17:00" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS (p.in.astropedia) ────────────────────────────
p.in.astropedia opus_id=co-iss-n1508963064 output=N1508963064_raw

# ── Step 3: Set polar output region in km × km ───────────────────────────────
# Both axes in km; Saturn's centre at (0,0).
# This box covers the A and B rings in the upper-left quadrant (~x=-140000..
# -50000, y=+70000..+130000) at the longitude of the observation (~230°).
g.region n=130000 s=70000 e=-50000 w=-140000 nsres=50 ewres=50

# ── Step 4: Project to polar ring-plane coordinates  ← this module ───────────
KDIR="$HOME/RSDATA/Saturn/kernels"
p.in.rings \
    input=N1508963064_raw output=N1508963064_polar \
    time="2005-10-23T14:17:00" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KDIR}/lsk/naif0012.tls,${KDIR}/sclk/cas00172.tsc,\
${KDIR}/ik/cas_iss_v10.ti,${KDIR}/fk/cas_v40.tf,\
${KDIR}/pck/cpck_rock_21Jan2011_merged.tpc,${KDIR}/pck/pck00010.tpc,\
${KDIR}/spk/050824R_SCPSE_05217_05257.bsp,\
${KDIR}/ck/05289_05294ra.bc"
r.colors map=N1508963064_polar color=grey
d.rast N1508963064_polar
```

The resulting raster shows the B-ring (outer edge ≈117 500 km) and A-ring
(outer edge ≈136 800 km) as arcs of circles with Saturn's centre at
(0, 0). Both axes label in km; `d.rast` respects the 1:1 aspect ratio.

**Optional — widen to the full ring system**

```bash
g.region n=145000 s=-145000 e=145000 w=-145000 res=200
# Re-run p.in.rings with projection=polar on additional images,
# then mosaic with r.patch.
```

## NOTES

- In `radlong` mode, set the region before running:
  `g.region n=<r_max> s=<r_min> e=<lon_max> w=<lon_min>`.
- In `polar` mode, set the region in km:
  `g.region n=<y_max> s=<y_min> e=<x_max> w=<x_min> res=<km_per_pixel>`.
  Saturn's center must lie inside or near the region for the geometry to work.
- Coverage of 30–60% of the output region is normal for a single oblique
  ring image; pixels outside the camera FOV are set to NULL.
- For steep viewing angles (spacecraft far above the ring plane), increase
  `grid=` (e.g. `grid=25`) to reduce bilinear model residuals.
- NAIF instrument IDs for Cassini ISS: NAC = `-82360`, WAC = `-82361`.
- The `polar` projection is equivalent to an orthographic projection of
  the ring plane as seen from infinite distance above the north pole.
  A single image covers only the arc of rings that fell inside the camera
  FOV; build a mosaic from multiple orbits to fill a complete annulus.
- The `filter=` value (e.g. `CL1/CL2`) is read from the PDS3 label keyword
  `FILTER_NAME` and stored verbatim in `planetary.json` alongside the image
  metadata for provenance.

## REQUIRED KERNELS

| Type | Example file | Purpose |
|------|-------------|---------|
| LSK | `naif0012.tls` | Leap-second kernel (UTC↔ET) |
| SCLK | `cas00172.tsc` | Spacecraft clock |
| IK | `cas_iss_v10.ti` | Instrument FOV definition |
| FK | `cas_v40.tf` | Spacecraft frame definition |
| PCK | `cpck_rock_21Jan2011_merged.tpc` | Planetary constants |
| SPK | `040701AP_SCPSE_04173_04236.bsp` | Ephemeris (SOI, Chain A) |
| SPK | `050824R_SCPSE_05217_05257.bsp` | Ephemeris (Rev 014, Chain B) |
| CK | `04183_04185ra.bc` | Spacecraft pointing (SOI, Chain A) |
| CK | `05289_05294ra.bc` | Spacecraft pointing (Rev 014, Chain B) |

Use **p.spice.find** to automatically download the correct kernels for
a given spacecraft and time.

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF kernels automatically
- [p.in.astropedia](p.in.astropedia.md) — fetch raw PDS3 images from OPUS
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.rings.stats](p.rings.stats.md) — radial brightness statistics
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster
- [p.cam2map](p.cam2map.md) — project planetary surface images
- [p.in.spice](p.in.spice.md) — generic SPICE-based image import

## AUTHOR

Yann Chemin
