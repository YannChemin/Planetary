# p.spice.find - Find and download NAIF SPICE kernels

## DESCRIPTION

**p.spice.find** discovers and downloads the SPICE kernels needed to work
with a spacecraft's imagery for a given UTC time.  It parses the NAIF
anonymous HTTP server's directory listings, selects the most accurate kernel
covering the requested time (preferring reconstructed-actual over predict),
and saves files into `$HOME/RSDATA/<Body>/kernels/<type>/`.

No pre-built index is required: the module derives time coverage directly
from NAIF's naming convention (`YYDOY_YYDOY` or `YYMMDD_YYMMDD`).

Currently supported spacecraft: **CASSINI**, MRO, LRO, MESSENGER, VEX.

## USAGE

```
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" [-l] [-f] [-m]
```

### Key parameters

| Parameter | Description |
|-----------|-------------|
| `spacecraft` | Spacecraft name (CASSINI, MRO, LRO, …) |
| `time` | UTC time of interest, ISO 8601 |
| `kernels` | Comma-separated types: `lsk,sclk,ik,fk,pck,spk,ck` (default: all) |
| `dest` | Root download directory (default: `$HOME/RSDATA/<Body>/kernels`) |
| `ck_type` | CK preference: `ra` reconstructed-actual (default), `ca_ISS`, `pa` predict |
| `-l` | List matching filenames without downloading |
| `-f` | Force re-download of existing files |
| `-m` | Write a meta-kernel (.tm) in `dest` referencing downloaded files |

## EXAMPLE

Fetch all kernels for the Cassini SOI ring image (N1467344155_2.IMG):

```bash
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" -m
```

This downloads:

| Type | File | Purpose |
|------|------|---------|
| LSK  | naif0012.tls | Leap-second kernel |
| SCLK | cas00172.tsc | Cassini spacecraft clock |
| IK   | cas_iss_v10.ti | ISS instrument model |
| FK   | cas_v40.tf | Cassini frame definitions |
| PCK  | cpck_rock_21Jan2011_merged.tpc | Planetary constants |
| SPK  | 040701AP_SCPSE_04173_04236.bsp | Ephemeris (sc + planets + moons) |
| CK   | 04183_04185ra.bc | Reconstructed pointing, DOY 183–185 |

And writes `~/RSDATA/Saturn/kernels/cassini_2004183.tm`.

## Saturn ring imaging pipelines

**p.spice.find** is Step 1 in both Cassini ring imaging chains.  The full
pipelines are described in detail in the [p.in.rings](p.in.rings.md) manual;
the summaries below show where this module fits in each chain.

### Chain A — SOI B-ring, radlong + RingCylindrical (analysis)

Image `N1467344155_2.IMG`, 2004-07-01T03:11:40, inner B ring, ≈86 400 km.
Full ready-to-run script: `$HOME/RSDATA/cassini_soi_b_ring.sh`.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

# ── Step 1: SPICE kernels  ← this module ─────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2004-07-01T03:11:40" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS (p.in.archive) ────────────────────────────
p.in.archive opus_id=co-iss-n1467344155 output=N1467344155_raw

# ── Step 3: Set radlong output region (p.in.rings) ───────────────────────────
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
Shows the outer A ring (Keeler Gap region) as an arc in polar ring-plane
coordinates.  Full ready-to-run script: `$HOME/RSDATA/cassini_rev014_polar.sh`.

```bash
# One-time: create an XY GRASS location for ring-plane coordinates
grass -c XY ~/grassdata/saturn_rings

KDIR="$HOME/RSDATA/Saturn/kernels"
IMAGE_MID_TIME="2004-07-01T03:52:49"
OPUS_ID="co-iss-n1467346624"
RAWMAP="N1467346624_polar_raw"
POLMAP="N1467346624_polar"

# ── Step 1: SPICE kernels  ← this module ─────────────────────────────────────
# Parses NAIF directory listings, selects the reconstructed-actual SPK and CK
# covering DOY 2004-183, downloads all 8 kernel types into $KDIR/type/.
# The output lines "selected: <file>" are parsed below to build KERNELS=.
SPICE_OUT=$(p.spice.find spacecraft=CASSINI time="${IMAGE_MID_TIME}" \
    dest="${KDIR}" 2>&1)
SPK_BASE=$(echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bsp' | tail -1)
CK_BASE=$( echo "${SPICE_OUT}" | grep -oP '(?<=selected: )\S+\.bc'  | tail -1)
LSK=$(find  "${KDIR}/lsk"  -name "*.tls"          | sort | tail -1)
SCLK=$(find "${KDIR}/sclk" -name "cas*.tsc"       | sort | tail -1)
IK=$(find   "${KDIR}/ik"   -name "cas_iss*.ti"    | sort | tail -1)
FK=$(find   "${KDIR}/fk"   -name "cas_v*.tf"      | sort | tail -1)
PCK1=$(find "${KDIR}/pck"  -name "cpck_rock*.tpc" | sort | tail -1)
PCK2=$(find "${KDIR}/pck"  -name "pck[0-9]*.tpc"  | sort | tail -1)
KERNELS="${LSK},${SCLK},${IK},${FK},${PCK1},${PCK2},\
${KDIR}/spk/${SPK_BASE},${KDIR}/ck/${CK_BASE}"

# ── Step 2: Fetch raw image from OPUS (p.in.archive) ──────────────────────
# product=raw: CISSCAL 4.0beta flags almost all ring pixels as sentinel values;
# the raw PDS3 image + per-column destripe (Step 3) is the usable path.
p.in.archive opus_id="${OPUS_ID}" output="${RAWMAP}" \
    product=raw --overwrite
g.region raster="${RAWMAP}"

# ── Step 3: Per-column destripe ────────────────────────────────────────────────
# ISS NAC CCD column-to-column bias (~10–50 DN/column) projects as diagonal
# stripes at 26.9° ring elevation.  Column baseline is estimated from
# background pixels only (below global 70th percentile) to exclude ring signal.
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
# Image centre → r=136 504 km, lon=67.17° → x≈52 970 km, y≈125 808 km.
# 455 × 435 km box at 1 km/pixel captures the outer A ring arc.
g.region n=126024 s=125589 e=53198 w=52743 nsres=1 ewres=1

# ── Step 5: Project to polar ring-plane coordinates (p.in.rings) ─────────────
p.in.rings \
    input="${RAWMAP}" output="${POLMAP}" \
    time="${IMAGE_MID_TIME}" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KERNELS}" grid=9 --overwrite

# ── Step 6: Display ────────────────────────────────────────────────────────────
r.colors -e map="${POLMAP}" color=grey
d.mon start=wx0 && d.rast "${POLMAP}"
```

## SEE ALSO

- [p.in.archive](p.in.archive.md) — fetch raw PDS3 images from OPUS
- [p.in.rings](p.in.rings.md) — project raw camera image to ring-plane space
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.rings.stats](p.rings.stats.md) — radial brightness statistics
- [p.in.spice](p.in.spice.md) — download generic planetary kernels (Moon, Mars…)
- [p.spice.config](p.spice.config.md) — set per-mapset SPICE configuration
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster map

## AUTHOR

Yann Chemin
