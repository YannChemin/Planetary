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

## Saturn ring imaging pipelines

**p.rings.project** is Step 5 in Chain A (SOI B-ring, radlong + RingCylindrical
analysis pipeline).  Chain B (Rev 014, polar display) does not require this
module because the `polar` projection from **p.in.rings** already produces a
Cartesian km×km map with a correct aspect ratio.

### Chain A — SOI B-ring, radlong + RingCylindrical (analysis)

Image `N1467344155_2.IMG`, 2004-07-01T03:11:40, inner B ring, 86 283–86 516 km.
Full ready-to-run script: `$HOME/RSDATA/cassini_soi_b_ring.sh`.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS (p.in.archive) ────────────────────────────
p.in.archive opus_id=co-iss-n1467344155 output=N1467344155_raw

# ── Step 3: Set radlong output region ────────────────────────────────────────
# north/south = ring_radius [km],  east/west = ring_longitude [deg]
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

# ── Step 5: RingCylindrical projection  ← this module ────────────────────────
p.rings.project \
    input=N1467344155_rings output=N1467344155_ringcyl \
    center_radius=86400 center_lon=66.39
r.colors map=N1467344155_ringcyl color=grey

# ── Step 6: Radial statistics (p.rings.stats) ─────────────────────────────────
p.rings.stats input=N1467344155_ringcyl \
    rmin=86250 rmax=86550 bin_width=5 \
    output=soi_bring_profile.csv radial=soi_bring_radial
```

The image covers:
- Ring radius: 86 283–86 516 km (inner B ring)
- Ring longitude: 66.27°–66.50°
- Image scale: ~0.23 km/pixel

### Chain B — Rev 014 B/A ring, polar (display)

Chain B uses `p.in.rings projection=polar` instead of `projection=radlong`,
producing a Cartesian km×km map directly.  **p.rings.project is not needed**
in this chain; the display step is simply `d.rast`.  See
[p.in.rings](p.in.rings.md) or `$HOME/RSDATA/cassini_rev014_polar.sh` for
the full polar pipeline.

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF kernels
- [p.in.archive](p.in.archive.md) — fetch raw PDS3 images from OPUS
- [p.in.rings](p.in.rings.md) — project raw camera image to ring_radius/ring_lon space
- [p.rings.stats](p.rings.stats.md) — statistical analysis of ring data
- [p.cam2map](p.cam2map.md) — project planetary surface images

## REFERENCES

- Porco, C.C. et al. (2005). Cassini Imaging Science: Initial results
  on Saturn's rings. *Science* 307:1226–1236.
  doi:[10.1126/science.1108056](https://doi.org/10.1126/science.1108056)

## AUTHOR

Yann Chemin
