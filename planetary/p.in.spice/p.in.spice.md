## DESCRIPTION

**p.in.spice** downloads and manages the NAIF SPICE kernels needed for
*real-ephemeris* computations (sub-solar and sub-Earth points) used by
*p.illumination.sunfraction*, *p.visibility.earth* and *p.spice.subpoint*.
It fetches a named **bundle** of kernels from the NAIF generic-kernels
server and writes a **meta-kernel** (`.tm`) that the other modules load
through the adopted CSPICE shared library.

Kernels can also be placed in the cache manually; in that case run the module
with **-m** to (re)build the meta-kernel from the files already present.

## KERNEL CACHE LOCATION AND STRUCTURE

Kernels and meta-kernels are stored under the **GRASS user configuration
directory** — the directory that contains your `GISRC` file, typically
`~/.grass8/` (or `~/.grass8.6/` depending on your GRASS version) — in a
`p_spice/` subdirectory:

```
<grass-config-dir>/p_spice/
├── kernels/                         # raw kernel files (shared by all bundles)
│   ├── naif0012.tls                 # leapseconds (time system)
│   ├── pck00011.tpc                 # planetary constants / orientation (text)
│   ├── de440s.bsp                   # Sun/planet/Moon ephemeris (positions)
│   ├── moon_080317.tf               # lunar frame kernel (defines MOON_ME)
│   └── moon_pa_de421_1900-2050.bpc  # lunar binary orientation (PA, DE421)
└── meta/                            # generated meta-kernels (one per bundle)
    ├── moon-me.tm
    └── moon-iau.tm
```

Override the root with `dest=` or the `$P_SPICE_CACHE` environment variable.
The cache is shared across all GRASS Locations/mapsets; the *active* meta-kernel
for a given mapset is selected with *p.spice.config*.

## BUNDLES

| Bundle | Frame | Notes |
|---|---|---|
| `moon-me` | `MOON_ME` | Lunar mean-Earth/polar-axis frame (DE421). **Matches LOLA/LRO DEM cartographic frame — recommended for the Moon.** |
| `moon-iau` | `IAU_MOON` | Lower-precision IAU orientation from the text PCK only (no binary kernels). |
| `mars` | `IAU_MARS` | Mars body-fixed frame from the text PCK. |
| `generic` | `IAU_<BODY>` | Ephemeris + planetary constants for any body with `IAU_<BODY>` orientation. |

List bundles and their kernel files with **-l**.

## PARAMETERS

| Parameter | Type | Default | Description |
|---|---|---|---|
| bundle | string | — | Kernel bundle: `moon-me`, `moon-iau`, `mars`, `generic` |
| dest | directory | `<grass-config>/p_spice` | Override cache root |
| timeout | integer | 600 | Per-file download timeout (seconds) |

## FLAGS

| Flag | Description |
|---|---|
| -l | List available bundles and exit |
| -d | Download missing kernels for the bundle from NAIF |
| -m | (Re)build the meta-kernel only, from kernels already present |
| -f | Force re-download even if a kernel file already exists |

## NOTES

- The kernels are public NASA/NAIF data but are **not** redistributed inside
  the package — they are fetched on first use (or placed manually), keeping
  the install small. Total size of the `moon-me` bundle is ~33 MB (most of it
  `de440s.bsp`).
- Download requires network access to `naif.jpl.nasa.gov`. For offline or
  air-gapped systems, copy the listed files into the `kernels/` directory and
  run with **-m**.
- The `moon-me` bundle reproduces the *"DE421 mean Earth/polar axis frame"*
  in which the LOLA polar DEMs are defined, so SPICE sub-point longitudes come
  out in the same east-positive selenographic convention as the DEM.

## EXAMPLES

### List bundles
```
p.in.spice -l
```

### Download the lunar mean-Earth bundle and build its meta-kernel
```
p.in.spice bundle=moon-me -d
```

### Rebuild a meta-kernel from manually-downloaded kernels
```
p.in.spice bundle=moon-me -m
```

### Activate the meta-kernel for the current mapset
```
p.spice.config meta=$HOME/.grass8/p_spice/meta/moon-me.tm -a
```

## SEE ALSO

*[p.spice.config](p.spice.config.md),
[p.spice.subpoint](p.spice.subpoint.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.visibility.earth](p.visibility.earth.md)*

## REFERENCES

- Acton, C.H. (1996) Ancillary data services of NASA's Navigation and
  Ancillary Information Facility. *Planetary and Space Science* 44, 65–70.
- NAIF generic kernels: <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/>

## AUTHOR

Yann Chemin
