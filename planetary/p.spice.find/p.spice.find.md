# p.spice.find - Find and download NAIF SPICE kernels

## DESCRIPTION

**p.spice.find** discovers and downloads the SPICE kernels needed to work with a spacecraft's imagery for a given UTC time.  It parses the NAIF anonymous HTTP server's directory listings, selects the most accurate kernel covering the requested time (preferring reconstructed-actual over predict), and saves files into `$HOME/RSDATA/<Body>/kernels/<type>/`.

No pre-built index is required: the module derives time coverage directly from NAIF's naming convention (`YYDOY_YYDOY` or `YYMMDD_YYMMDD`).

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

## SEE ALSO

- [p.in.spice](p.in.spice.md) — download generic planetary kernels (Moon, Mars…)
- [p.spice.config](p.spice.config.md) — set per-mapset SPICE configuration
- [p.spiceinit](p.spiceinit.md) — attach kernels to a GRASS raster map

## AUTHOR

Yann Chemin
