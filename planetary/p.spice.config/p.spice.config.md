## DESCRIPTION

**p.spice.config** sets or shows the active SPICE configuration for the
**current GRASS mapset**: which meta-kernel to load, the target body, the
body-fixed reference frame, and the observer body. The settings are stored in
the mapset `VAR` file (via `g.gisenv store=mapset`), so every module that does
real-ephemeris work — *p.illumination.sunfraction*, *p.visibility.earth*,
*p.spice.subpoint* — reads one shared source of truth instead of taking raw
kernel paths on each call.

### Auto-detection from the CRS

With the **-a** flag the module infers the **target body** and **body-fixed
frame** from the Location's coordinate reference system (the semi-major axis
reported by `g.proj -g`). For example a sphere of radius 1 737 400 m is
recognised as the Moon with frame `MOON_ME`. This guarantees the SPICE
sub-point longitudes are produced in the same east-positive planetocentric
convention as the DEM, which is the usual source of silent errors when mixing
SPICE with GIS data. Explicit `target=`/`frame=` options override detection.

If `frame=` is not given and the meta-kernel was produced by *p.in.spice*, the
frame recorded in the meta-kernel's comment block is used.

## STORED KEYS

| Key (mapset VAR) | Meaning |
|---|---|
| `P_SPICE_META` | Absolute path to the active meta-kernel (`.tm`) |
| `P_SPICE_TARGET` | Target body name (e.g. `MOON`, `MARS`) |
| `P_SPICE_FRAME` | Body-fixed frame (e.g. `MOON_ME`, `IAU_MARS`) |
| `P_SPICE_OBSERVER` | Observer body for visibility (default `EARTH`) |

## PARAMETERS

| Parameter | Type | Default | Description |
|---|---|---|---|
| meta | file | — | Meta-kernel (`.tm`) to activate (from *p.in.spice*) |
| target | string | — | SPICE target body name |
| frame | string | — | Body-fixed reference frame |
| observer | string | EARTH | Observer body for sub-observer/visibility |

## FLAGS

| Flag | Description |
|---|---|
| -p | Print the current mapset configuration and exit |
| -a | Auto-detect target body and frame from the Location CRS |

## NOTES

- The configuration is **per-mapset**: different mapsets in the same Location
  can use different kernels or frames.
- When a meta-kernel is set and the CSPICE library is available, the module
  runs a quick test sub-solar computation and reports it, so you get immediate
  feedback that the kernels load and the frame is valid.

## EXAMPLES

### Auto-detect body/frame and activate a meta-kernel
```
p.spice.config meta=$HOME/.grass8/p_spice/meta/moon-me.tm -a
```

### Show the current configuration
```
p.spice.config -p
```

### Configure Mars explicitly
```
p.spice.config meta=.../mars.tm target=MARS frame=IAU_MARS
```

## SEE ALSO

*[p.in.spice](p.in.spice.md),
[p.spice.subpoint](p.spice.subpoint.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.visibility.earth](p.visibility.earth.md),
[g.proj](https://grass.osgeo.org/grass-stable/manuals/g.proj.html),
[g.gisenv](https://grass.osgeo.org/grass-stable/manuals/g.gisenv.html)*

## AUTHOR

Yann Chemin
