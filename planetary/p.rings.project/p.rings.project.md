# p.rings.project - Project a raster to/from ring-plane cylindrical coordinates

## DESCRIPTION

**p.rings.project** applies or inverts the RingCylindrical projection for
planetary ring imaging.  It operates on a raster that is already in
ring_radius/ring_longitude coordinate space — created by **p.in.rings** —
and reprojects it to a Cartesian (x, y) ring-plane map, or vice versa.

The current GRASS computational region defines the output grid. In the
**forward direction** (default), east/west of the region are interpreted as
ring_longitude [deg] and north/south as ring_radius [km].  In the **inverse
direction** (`-i` flag), the roles are reversed.

**p.rings.project does not use SPICE** — it is a pure geometric
coordinate transform.  The raw-camera → ring_radius/ring_lon step is
handled by **p.in.rings**.

## USAGE

```
p.rings.project input=<raster> output=<raster> \
    center_radius=<km> [center_lon=<deg>] [-i] [-c]
```

| Parameter | Description |
|---|---|
| `center_radius` | Ring radius at the projection centre [km] |
| `center_lon` | Ring longitude at the projection centre [deg], default 0 |
| `-i` | Inverse: (x,y) → (ring_radius, ring_lon) |
| `-c` | Clockwise ring longitude direction |

## Saturn ring chain processing example

Full pipeline for Cassini ISS NAC image `N1467344155_2.IMG`
(SOI approach, 2004-07-01T03:11:40, inner B ring, 86283–86516 km).

A ready-to-run script is available at `$HOME/RSDATA/cassini_soi_b_ring.sh`.
Run it from inside the GRASS session described below.

**Step 0 — Create XY GRASS location** (run once, *before* starting GRASS)

```sh
grass -c XY ~/grassdata/saturn_rings
```

Ring-plane coordinates (ring_radius in km, ring_lon in degrees) are
dimensionless XY values; the XY system avoids any geographic projection.

**Step 1 — Download SPICE kernels** (`p.spice.find`)

```sh
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels
```

Downloads LSK, SCLK, IK, FK, PCK, SPK, and CK for the requested time.

**Step 2 — Import raw PDS3 image** (`r.in.gdal`)

```sh
r.in.gdal -o \
    input=$HOME/RSDATA/Saturn/N1467344155_2.IMG \
    output=N1467344155_raw
```

**Step 3 — Set ring-plane region** (`g.region`)

```sh
# north/south = ring_radius [km],  east/west = ring_longitude [deg]
g.region n=86550 s=86250 e=66.55 w=66.25 nsres=0.25 ewres=0.0003
```

**Step 4 — Project raw image to ring-plane coordinates** (`p.in.rings`)

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

**Step 5 — Apply RingCylindrical projection** ← *this module*

```sh
p.rings.project \
    input=N1467344155_rings \
    output=N1467344155_ringcyl \
    center_radius=86400 center_lon=66.39
r.colors map=N1467344155_ringcyl color=grey
d.rast N1467344155_ringcyl
```

The image covers:
- Ring radius: 86283–86516 km (inner B ring)
- Ring longitude: 66.27°–66.50°
- Image scale: ~0.23 km/pixel

## SEE ALSO

- [p.in.rings](p.in.rings.md) — project raw camera image to ring_radius/ring_lon space
- [p.spice.find](p.spice.find.md) — automatically download NAIF kernels
- [p.rings.stats](p.rings.stats.md) — statistical analysis of ring data
- [p.cam2map](p.cam2map.md) — project planetary surface images

## REFERENCES

- Porco, C.C. et al. (2005). Cassini Imaging Science: Initial results
  on Saturn's rings. *Science* 307:1226–1236.
  doi:[10.1126/science.1108056](https://doi.org/10.1126/science.1108056)

## AUTHOR

Yann Chemin
