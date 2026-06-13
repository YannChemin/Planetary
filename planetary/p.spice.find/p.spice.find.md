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

# ── Step 2: Raw image from OPUS (p.in.astropedia) ────────────────────────────
p.in.astropedia opus_id=co-iss-n1467344155 output=N1467344155_raw

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

### Chain B — A ring / F ring, polar (display)

Image `N1498508609_1.IMG`, 2005-06-26T19:55:52, observer ring elevation +38.7°.
Full ready-to-run script: `$HOME/RSDATA/cassini_rev014_polar.sh`.

```bash
# ── Step 1: SPICE kernels  ← this module ─────────────────────────────────────
p.spice.find spacecraft=CASSINI time="2005-06-26T19:55:52" \
    dest=$HOME/RSDATA/Saturn/kernels

# ── Step 2: Raw image from OPUS (p.in.astropedia) ────────────────────────────
p.in.astropedia opus_id=co-iss-n1498508609 output=N1498508609_raw

# ── Step 3: Set polar output region in km × km ───────────────────────────────
g.region n=130000 s=70000 e=-50000 w=-140000 nsres=50 ewres=50

# ── Step 4: Project to polar ring-plane coordinates (p.in.rings) ─────────────
KDIR="$HOME/RSDATA/Saturn/kernels"
p.in.rings \
    input=N1498508609_raw output=N1498508609_polar \
    time="2005-06-26T19:55:52" instrument=-82360 \
    spacecraft=CASSINI body=SATURN frame=IAU_SATURN \
    projection=polar filter="CL1/CL2" \
    kernels="${KDIR}/lsk/naif0012.tls,${KDIR}/sclk/cas00172.tsc,\
${KDIR}/ik/cas_iss_v10.ti,${KDIR}/fk/cas_v40.tf,\
${KDIR}/pck/cpck_rock_21Jan2011_merged.tpc,${KDIR}/pck/pck00010.tpc,\
${KDIR}/spk/050824R_SCPSE_05217_05257.bsp,\
${KDIR}/ck/05289_05294ra.bc"
r.colors map=N1498508609_polar color=grey
d.rast N1498508609_polar
```

## SEE ALSO

- [p.in.astropedia](p.in.astropedia.md) — fetch raw PDS3 images from OPUS
- [p.in.rings](p.in.rings.md) — project raw camera image to ring-plane space
- [p.rings.project](p.rings.project.md) — RingCylindrical projection
- [p.rings.stats](p.rings.stats.md) — radial brightness statistics
- [p.in.spice](p.in.spice.md) — download generic planetary kernels (Moon, Mars…)
- [p.spice.config](p.spice.config.md) — set per-mapset SPICE configuration
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster map

## AUTHOR

Yann Chemin
