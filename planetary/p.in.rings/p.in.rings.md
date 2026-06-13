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

# radlong mode — region in km × degrees (default, analysis use)
g.region n=86550 s=86250 e=66.55 w=66.25 nsres=0.25 ewres=0.0003

# polar mode — region in km × km (display use, isotropic)
g.region n=130000 s=70000 e=-50000 w=-140000 res=50
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

## Saturn ring chain processing example — SOI B-ring (radlong mode)

The full ring processing pipeline for the Cassini SOI B-ring image
(`N1467344155_2.IMG`, 2004-07-01T03:11:40, inner B ring, 86283–86516 km)
runs in five steps. A ready-to-run script is at
`$HOME/RSDATA/cassini_soi_b_ring.sh`.

**Step 0 — Download SPICE kernels** (`p.spice.find`)

```sh
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels
```

Downloads LSK, SCLK, IK, FK, PCK, SPK and CK automatically.

**Step 1 — Import the raw PDS3 image** (`r.in.gdal`)

```sh
r.in.gdal -o input=$HOME/RSDATA/Saturn/N1467344155_2.IMG \
              output=N1467344155_raw
```

**Step 2 — Set the ring-plane output region** (`g.region`)

```sh
# north/south = ring_radius [km],  east/west = ring_longitude [deg]
g.region n=86550 s=86250 e=66.55 w=66.25 nsres=0.25 ewres=0.0003
```

**Step 3 — Project to ring_radius / ring_lon space** ← *this module*

```sh
p.in.rings \
    input=N1467344155_raw \
    output=N1467344155_rings \
    time="2004-07-01T03:11:40.288" \
    instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=radlong \
    filter="CL1/CL2" \
    kernels="$HOME/RSDATA/Saturn/kernels/lsk/naif0012.tls,\
$HOME/RSDATA/Saturn/kernels/sclk/cas00172.tsc,\
$HOME/RSDATA/Saturn/kernels/ik/cas_iss_v10.ti,\
$HOME/RSDATA/Saturn/kernels/fk/cas_v40.tf,\
$HOME/RSDATA/Saturn/kernels/pck/cpck_rock_21Jan2011_merged.tpc,\
$HOME/RSDATA/Saturn/kernels/pck/pck00010.tpc,\
$HOME/RSDATA/Saturn/kernels/spk/040701AP_SCPSE_04173_04236.bsp,\
$HOME/RSDATA/Saturn/kernels/ck/04183_04185ra.bc"
```

**Step 4 — Apply RingCylindrical projection** (`p.rings.project`)

```sh
p.rings.project \
    input=N1467344155_rings \
    output=N1467344155_ringcyl \
    center_radius=86400 center_lon=66.39
```

**Step 5 — Display**

```sh
r.colors map=N1467344155_ringcyl color=grey
d.rast N1467344155_ringcyl
```

## Saturn ring chain processing example — Rev 014 B-ring polar view

This second example uses a Cassini Rev 014 ISS NAC image taken on
2005-10-23 when Cassini was approximately 10° above Saturn's ring plane.
At this elevation the B and A rings appear as visible arcs in the image.
Using `projection=polar`, both axes are in km so the map renders with
correct aspect ratio (rings as arcs of circles centred on Saturn at the
origin). A ready-to-run script is at `$HOME/RSDATA/cassini_rev014_polar.sh`.

**Step 0 — Download SPICE kernels** (`p.spice.find`)

```sh
p.spice.find spacecraft=CASSINI time="2005-10-23T14:17:00" \
    dest=$HOME/RSDATA/Saturn/kernels
```

**Step 1 — Import the raw PDS3 image** (`r.in.gdal`)

```sh
r.in.gdal -o \
    input=$HOME/RSDATA/Saturn/N1508963064_2.IMG \
    output=N1508963064_raw
```

**Step 2 — Set the polar ring-plane region** (`g.region`)

```sh
# Both axes in km; Saturn's centre at (0,0).
# This box covers the A and B rings in the upper-left quadrant
# of the ring plane at the longitude of the observation (~230°).
g.region n=130000 s=70000 e=-50000 w=-140000 nsres=50 ewres=50
```

**Step 3 — Project to polar ring-plane coordinates** ← *this module*

```sh
p.in.rings \
    input=N1508963064_raw \
    output=N1508963064_polar \
    time="2005-10-23T14:17:00" \
    instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar \
    kernels="$HOME/RSDATA/Saturn/kernels/lsk/naif0012.tls,\
$HOME/RSDATA/Saturn/kernels/sclk/cas00172.tsc,\
$HOME/RSDATA/Saturn/kernels/ik/cas_iss_v10.ti,\
$HOME/RSDATA/Saturn/kernels/fk/cas_v40.tf,\
$HOME/RSDATA/Saturn/kernels/pck/cpck_rock_21Jan2011_merged.tpc,\
$HOME/RSDATA/Saturn/kernels/pck/pck00010.tpc,\
$HOME/RSDATA/Saturn/kernels/spk/050824R_SCPSE_05217_05257.bsp,\
$HOME/RSDATA/Saturn/kernels/ck/05289_05294ra.bc"
```

The resulting raster shows the B-ring and A-ring as arcs of circles with
Saturn's center at (0, 0). Both axes label in km; `d.rast` and GRASS
display tools respect the 1:1 aspect ratio automatically.

**Step 4 — Optional: widen to the full ring system**

To see all rings in a single polar map (D → F ring), use a larger region:

```sh
g.region n=145000 s=-145000 e=145000 w=-145000 res=200
# then re-run p.in.rings with projection=polar on all available ring images
# and mosaic with r.patch
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

## REQUIRED KERNELS

| Type | Example file | Purpose |
|------|-------------|---------|
| LSK | `naif0012.tls` | Leap-second kernel (UTC↔ET) |
| SCLK | `cas00172.tsc` | Spacecraft clock |
| IK | `cas_iss_v10.ti` | Instrument FOV definition |
| FK | `cas_v40.tf` | Spacecraft frame definition |
| PCK | `cpck_rock_21Jan2011_merged.tpc` | Planetary constants |
| SPK | `040701AP_SCPSE_04173_04236.bsp` | Ephemeris (SOI) |
| SPK | `050824R_SCPSE_05217_05257.bsp` | Ephemeris (Rev 014) |
| CK | `04183_04185ra.bc` | Spacecraft pointing (SOI) |
| CK | `05289_05294ra.bc` | Spacecraft pointing (Rev 014) |

Use **p.spice.find** to automatically download the correct kernels for
a given spacecraft and time.

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF kernels automatically
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster
- [p.cam2map](p.cam2map.md) — project planetary surface images
- [p.in.spice](p.in.spice.md) — generic SPICE-based image import

## AUTHOR

Yann Chemin
