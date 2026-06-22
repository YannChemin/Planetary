# p.rings.stats - Radial statistics of ring brightness

## DESCRIPTION

*p.rings.stats* computes radial statistics of ring brightness from a
ring-plane projected raster (output of *p.rings.project*). Pixels are
binned into annular rings of width **bin_width** (km) from **rmin** to
**rmax**, and statistics (mean, median, standard deviation, min, max)
are computed for each radial bin, optionally as a function of longitude.

Output: a CSV table and optionally a radial profile GRASS raster of
ring brightness versus radius.

## EXAMPLES

Compute a 5 km-wide radial brightness profile of the SOI B-ring:

```sh
p.rings.stats input=N1467344155_ringcyl \
    rmin=86250 rmax=86550 bin_width=5 \
    output=soi_bring_profile.csv radial=soi_bring_radial
```

## Saturn ring imaging pipeline — Chain A (full context)

**p.rings.stats** is the final analysis step in Chain A (SOI B-ring, radlong +
RingCylindrical pipeline). It consumes the output of **p.rings.project**.
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

# ── Step 5: RingCylindrical projection (p.rings.project) ─────────────────────
p.rings.project \
    input=N1467344155_rings output=N1467344155_ringcyl \
    center_radius=86400 center_lon=66.39
r.colors map=N1467344155_ringcyl color=grey

# ── Step 6: Radial statistics  ← this module ──────────────────────────────────
p.rings.stats input=N1467344155_ringcyl \
    rmin=86250 rmax=86550 bin_width=5 \
    output=soi_bring_profile.csv radial=soi_bring_radial
```

The CSV output columns are: `r_km`, `mean`, `median`, `std`, `min`, `max`
(plus `lon_deg` when longitude binning is enabled). The optional radial
raster `soi_bring_radial` is in the same DN units as the input map and can
be displayed or analysed with standard GRASS raster tools.

## NOTES

The input raster must be in ring-plane coordinates produced by
*p.rings.project* (Chain A, radlong path). The `polar` output of
*p.in.rings* (Chain B) is in km×km Cartesian space; to compute radial
profiles from it, convert to ring_radius first with `p.rings.project -i`.

## REFERENCES

- Hedman, M.M. & Nicholson, P.D. (2013). Kronoseismology: Using
  density waves in Saturn's C ring to probe the planet's interior.
  *Astronomical Journal* 146(1):12.
  doi:[10.1088/0004-6256/146/1/12](https://doi.org/10.1088/0004-6256/146/1/12)

## SEE ALSO

- [p.spice.find](p.spice.find.md) — download NAIF SPICE kernels
- [p.in.archive](p.in.archive.md) — fetch raw PDS3 images from OPUS
- [p.in.rings](p.in.rings.md) — project raw camera image to ring-plane space
- [p.rings.project](p.rings.project.md) — RingCylindrical projection (prerequisite)

## AUTHOR

Yann Chemin
