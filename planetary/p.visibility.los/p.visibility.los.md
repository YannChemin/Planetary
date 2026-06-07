## DESCRIPTION

**p.visibility.los** computes the maximum terrain horizon elevation angle map
and the line-of-sight (LOS) viewshed to one or more base/relay stations.

### Horizon map

*r.horizon* is called in step mode for `directions` evenly-spaced azimuths.
The output `<prefix>_horizon_max` is the maximum horizon elevation across all
directions — a proxy for sky blockage. Low values indicate good locations for
solar panels and antennas.

### LOS viewshed

For each site in `sites=`, *r.viewshed* computes the binary visibility raster.
A multi-site coverage map counts how many sites are simultaneously visible
from each pixel.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `body` | — | Body descriptor JSON (for curvature correction) |
| `directions` | 16 | Number of horizon directions (evenly spaced 0–360°) |
| `sites` | — | Base/relay sites as `lon,lat` pairs separated by semicolons |
| `observer_elev` | 2.0 | Observer (lander/rover) height above ground (metres) |
| `target_elev` | 10.0 | Target (relay antenna) height above ground (metres) |
| `max_distance` | 0 | Maximum LOS search distance in metres (0 = unlimited) |
| `scan_res` | 0 | Working resolution for horizon computation (0 = native DEM) |
| `prefix` | `los` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_horizon_max` | Maximum terrain horizon elevation (degrees) |
| `<prefix>_los_site_N` | Binary LOS to site N (1=visible) |
| `<prefix>_los_coverage` | Count of sites simultaneously visible from each pixel |

## EXAMPLES

```bash
# Horizon map only
p.visibility.los dem=lola_5m directions=32 prefix=los

# LOS to two relay stations
p.visibility.los dem=lola_5m \
                 sites="17.5,-81.44;5.95,-80.33" \
                 observer_elev=2.0 target_elev=10.0 prefix=los
```

## PERFORMANCE

Set `HORIZON_BACKEND=gpu` in the environment to swap the internal
*r.horizon* call for *[p.horizon.gpu](p.horizon.gpu.md)* (OpenCL). Requires
a conformal CRS (UTM, polar stereographic, Lambert Conformal Conic, Mercator
and variants). On a Quadro P1000 the 16-azimuth precompute on a
3000×3000@5 m DEM drops from ~50 min to seconds.

```bash
HORIZON_BACKEND=gpu grass ~/grassdata/Moon_SouthPole_5m/mapset --exec \
    p.visibility.los dem=lola_5m directions=16 prefix=los
```

## NOTES

The horizon-maximum raster is a useful standalone product for antenna placement: it directly quantifies the minimum elevation clearance a communications link needs. LOS viewsheds use Bresenham ray tracing via *r.viewshed* and are not corrected for atmospheric refraction or Earth curvature; both effects are negligible for planetary surface applications.

## SEE ALSO

*[p.visibility.earth](p.visibility.earth.md),
[p.visibility.orbiter](p.visibility.orbiter.md),
[r.horizon](https://grass.osgeo.org/grass-stable/manuals/r.horizon.html),
[r.viewshed](https://grass.osgeo.org/grass-stable/manuals/r.viewshed.html)*

## REFERENCES

- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
