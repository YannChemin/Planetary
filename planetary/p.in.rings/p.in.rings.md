# p.in.rings - Project a raw camera image to ring-plane coordinates using SPICE

## DESCRIPTION

**p.in.rings** reprojects a raw spacecraft camera image (in pixel/sample
coordinates) into ring-plane (ring_radius, ring_longitude) coordinates
using NAIF SPICE kernels.

For each output pixel in the current GRASS computational region — where
**north/south encode ring_radius [km]** and **east/west encode
ring_longitude [degrees]** — the module back-projects to the camera pixel
using a bilinear ring-geometry model and samples the input DN value with
bilinear interpolation (or nearest-neighbour with `-n`).

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
    [kernels=k1,k2,...] [grid=9] [-n]
```

| Parameter | Description |
|---|---|
| `input` | Raw raster in pixel/sample coordinates (W=0 E=ncols S=0 N=nlines) |
| `output` | Output raster in ring_radius/ring_lon coordinates |
| `time` | Image mid-time, UTC ISO-8601 |
| `instrument` | NAIF instrument ID (e.g. `-82360` for Cassini ISS NAC) |
| `spacecraft` | NAIF spacecraft name (default `CASSINI`) |
| `body` | Central body (default `SATURN`) |
| `frame` | Body-fixed frame (default `IAU_SATURN`) |
| `kernels` | Comma-separated kernel paths; if omitted, loads from mapset `spice/` |
| `grid` | Geometry sampling grid size N×N (default 9) |
| `-n` | Nearest-neighbour sampling instead of bilinear |

## Saturn ring chain processing example

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

The raster is imported in pixel/sample coordinates (W=0 E=1024 S=0 N=1024).

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
    kernels="$HOME/RSDATA/Saturn/kernels/lsk/naif0012.tls,\
$HOME/RSDATA/Saturn/kernels/sclk/cas00172.tsc,\
$HOME/RSDATA/Saturn/kernels/ik/cas_iss_v10.ti,\
$HOME/RSDATA/Saturn/kernels/fk/cas_v40.tf,\
$HOME/RSDATA/Saturn/kernels/pck/cpck_rock_21Jan2011_merged.tpc,\
$HOME/RSDATA/Saturn/kernels/pck/pck00010.tpc,\
$HOME/RSDATA/Saturn/kernels/spk/040701AP_SCPSE_04173_04236.bsp,\
$HOME/RSDATA/Saturn/kernels/ck/04183_04185ra.bc"
```

Produces `N1467344155_rings` in ring_radius/ring_lon coordinates.

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

## NOTES

- The output region must be set before running the module.  Use
  `g.region n=<r_max> s=<r_min> e=<lon_max> w=<lon_min>` to define it.
- Coverage of 30–60% of the output region is normal for a single
  oblique ring image; pixels outside the camera FOV are set to NULL.
- For very oblique geometries (spacecraft above the ring plane at steep
  angles), the bilinear approximation may be less accurate.  Increase
  `grid=` (e.g. `grid=25`) for higher-fidelity geometry sampling.
- NAIF instrument IDs for Cassini ISS: NAC = `-82360`, WAC = `-82361`.

## REQUIRED KERNELS

| Type | Example file | Purpose |
|------|-------------|---------|
| LSK | `naif0012.tls` | Leap-second kernel (UTC↔ET) |
| SCLK | `cas00172.tsc` | Spacecraft clock |
| IK | `cas_iss_v10.ti` | Instrument FOV definition |
| FK | `cas_v40.tf` | Spacecraft frame definition |
| PCK | `cpck_rock_21Jan2011_merged.tpc` | Planetary constants |
| SPK | `040701AP_SCPSE_04173_04236.bsp` | Ephemeris |
| CK | `04183_04185ra.bc` | Spacecraft pointing |

Use **p.spice.find** to automatically download the correct kernels for
a given spacecraft and time.

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF kernels automatically
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster
- [p.cam2map](p.cam2map.md) — project planetary surface images

## AUTHOR

Yann Chemin
