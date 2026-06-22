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

# ── Step 2: Raw image from OPUS (p.in.archive) ────────────────────────────
p.in.archive opus_id=co-iss-n1467344155 output=N1467344155_raw

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

### Chain B — outer A ring polar (display)

Image `N1467346624_2.IMG`, 2004-07-01T03:52:49, observer ring elevation +26.9°.
Shows the outer A ring (Keeler Gap region, 136 272–136 649 km) as a bright arc
in polar ring-plane coordinates. The CISSCAL 4.0beta calibrated product for
this image flags nearly all ring pixels as sentinel values, so the pipeline
uses the **raw** image and removes the ISS CCD column-to-column bias with a
per-column destripe step before projection.

Ready-to-run script: `$HOME/RSDATA/cassini_rev014_polar.sh`

**One-time setup**

```bash
# Create an XY GRASS location (ring-plane has no geographic CRS)
grass -c XY ~/grassdata/saturn_rings
```

**Run**

```bash
grass ~/grassdata/saturn_rings/PERMANENT \
    --exec bash $HOME/RSDATA/cassini_rev014_polar.sh
# or, inside an active GRASS session:
bash $HOME/RSDATA/cassini_rev014_polar.sh
```

**Full annotated script**

```bash
KDIR="$HOME/RSDATA/Saturn/kernels"
DATADIR="$HOME/RSDATA/Misc"
IMAGE_MID_TIME="2004-07-01T03:52:49"
OPUS_ID="co-iss-n1467346624"
RAWMAP="N1467346624_polar_raw"
POLMAP="N1467346624_polar"

# ── Step 1: SPICE kernels (p.spice.find) ──────────────────────────────────────
# Downloads LSK, SCLK, IK, FK, PCK, SPK, CK into $KDIR; selects the
# reconstructed-actual SPK/CK covering DOY 2004-183 automatically.
SPICE_OUT=$(p.spice.find spacecraft=CASSINI time="${IMAGE_MID_TIME}" \
    dest="${KDIR}" 2>&1)
SPK_BASE=$(echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bsp' | tail -1)
CK_BASE=$( echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bc'  | tail -1)
LSK=$(find  "${KDIR}/lsk"  -name "*.tls"         | sort | tail -1)
SCLK=$(find "${KDIR}/sclk" -name "cas*.tsc"      | sort | tail -1)
IK=$(find   "${KDIR}/ik"   -name "cas_iss*.ti"   | sort | tail -1)
FK=$(find   "${KDIR}/fk"   -name "cas_v*.tf"     | sort | tail -1)
PCK1=$(find "${KDIR}/pck"  -name "cpck_rock*.tpc"| sort | tail -1)
PCK2=$(find "${KDIR}/pck"  -name "pck[0-9]*.tpc" | sort | tail -1)
KERNELS="${LSK},${SCLK},${IK},${FK},${PCK1},${PCK2},\
${KDIR}/spk/${SPK_BASE},${KDIR}/ck/${CK_BASE}"

# ── Step 2: Fetch raw image from OPUS (p.in.archive) ──────────────────────
# product=raw: the CISSCAL 4.0beta calibrated product flags almost all ring
# pixels as sentinel values (only cosmic rays survive as "valid"), so the raw
# PDS3 image is the usable product for projection.
p.in.archive opus_id="${OPUS_ID}" output="${RAWMAP}" \
    product=raw --overwrite
g.region raster="${RAWMAP}"

# ── Step 3: Per-column destripe ────────────────────────────────────────────────
# The ISS NAC CCD has column-to-column electronic bias (~10–50 DN/column).
# At 26.9° ring elevation the bias projects as diagonal stripes across the
# ring-plane output.  We estimate the per-column baseline from background
# pixels only (below the global 70th-percentile threshold) so that the
# bright ring signal does not contaminate the column bias estimate.
python3 - "${RAWMAP}" << 'PYEOF'
import sys, tempfile, os
import numpy as np
import grass.script as gs

name = sys.argv[1]
reg  = gs.region()
nr, nc = int(reg["rows"]), int(reg["cols"])
tmp = tempfile.mktemp(suffix=".bin")
gs.run_command("r.out.bin", input=name, output=tmp,
               bytes=4, flags="f", null="-9999", quiet=True)
raw = np.fromfile(tmp, dtype=np.float32).reshape(nr, nc).astype(np.float64)
null_mask = (raw == -9999.0)
raw[null_mask] = np.nan
global_thresh = np.nanpercentile(raw, 70)
background    = np.where(raw < global_thresh, raw, np.nan)
col_bias      = np.nanmedian(background, axis=0)
all_bright    = np.isnan(col_bias)
if np.any(all_bright):
    col_bias[all_bright] = np.nanmedian(raw[:, all_bright], axis=0)
raw -= col_bias[np.newaxis, :]
raw[null_mask] = -9999.0
raw.astype(np.float32).tofile(tmp)
gs.run_command("r.in.bin", input=tmp, output=name, bytes=4, flags="f",
               north=reg["n"], south=reg["s"], east=reg["e"], west=reg["w"],
               rows=nr, cols=nc, anull="-9999", overwrite=True, quiet=True)
os.unlink(tmp)
PYEOF

# ── Step 4: Set polar ring-plane output region (km × km) ─────────────────────
# p.in.rings reports "Polar centre hint: x≈52970 km, y≈125808 km" when run
# on any region covering Saturn.  This tight 455×435 km box at 1 km/pixel
# centres on that hint and captures the full arc width of the outer A ring.
g.region n=126024 s=125589 e=53198 w=52743 nsres=1 ewres=1

# ── Step 5: Project to polar ring-plane coordinates  ← this module ───────────
# For each output km×km pixel p.in.rings:
#   1. constructs the pixel look-vector in IAU_SATURN via SPICE CK/SPK,
#   2. intersects the ray with z=0 (ring plane),
#   3. records x,y in km; bilinear-samples the destriped raw image.
# Valid pixels: ~25% of the output region (single oblique image).
p.in.rings \
    input="${RAWMAP}" output="${POLMAP}" \
    time="${IMAGE_MID_TIME}" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KERNELS}" grid=9 --overwrite

# ── Step 6: Display ────────────────────────────────────────────────────────────
# Histogram-equalized grey: ring arc peaks at ~950 DN while background noise
# is ~±5 DN; -e maps the actual DN distribution so the arc is visually clear.
r.colors -e map="${POLMAP}" color=grey
d.mon start=wx0 && d.rast "${POLMAP}"
```

**Output**

`N1467346624_polar` — outer A ring arc (136 272–136 649 km) near polar
coordinates (x≈52 970, y≈125 808) km in IAU_SATURN, displayed as a curved
bright band against the dark ring-plane background.  Saturn's centre is at
(0, 0); both axes in km; `d.rast` renders with correct 1:1 aspect ratio.

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
- [p.in.archive](p.in.archive.md) — fetch raw PDS3 images from OPUS
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.rings.stats](p.rings.stats.md) — radial brightness statistics
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster
- [p.cam2map](p.cam2map.md) — project planetary surface images
- [p.in.spice](p.in.spice.md) — generic SPICE-based image import

## AUTHOR

Yann Chemin
