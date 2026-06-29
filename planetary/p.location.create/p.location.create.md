## DESCRIPTION

*p.location.create* creates a new GRASS GIS location configured with the
correct ellipsoid and projection for a solar system body. It sets a
full-body default region at the requested resolution so the new location
is immediately usable without additional `g.region` setup.

**Must be run from inside an existing GRASS session** (any location/mapset).
The new location is created in the same GISDBASE as the current session.

### Body database

Built-in bodies (IAU 2015/2018 reference radii):

| `body=` | Name | a (m) | b (m) |
|---|---|---|---|
| `mars` | Mars | 3 396 190 | 3 376 200 |
| `moon` | Moon | 1 737 400 | 1 737 400 (sphere) |
| `venus` | Venus | 6 051 800 | 6 051 800 (sphere) |
| `mercury` | Mercury | 2 439 700 | 2 439 700 (sphere) |
| `titan` | Titan | 2 574 730 | 2 574 730 (sphere) |
| `ceres` | Ceres | 482 060 | 445 940 |
| `enceladus` | Enceladus | 256 600 | 248 300 |
| `europa` | Europa | 1 562 090 | 1 562 090 (sphere) |
| `custom` | — | `semi_major=` | `semi_minor=` |

### Projections

| `projection=` | PROJ.4 | Typical use |
|---|---|---|
| `latlong` | `+proj=longlat` | GIS analysis, band-depth maps, baselines |
| `eqc` | Equidistant cylindrical | Standard planetary base map (simple cylindrical) |
| `sinu` | Sinusoidal | Equal-area full-globe maps |
| `north_stereo` | Polar stereographic N | Arctic / north polar cap |
| `south_stereo` | Polar stereographic S | South polar cap |
| `merc` | Mercator | Low-latitude navigation strips |
| `lcc` | Lambert Conformal Conic | Regional mid-latitude maps (two std. parallels) |
| `laea` | Lambert Azimuthal Equal-Area | Hemispheric equal-area |
| `ortho` | Orthographic | Visualisation only |

### Resolution

For `latlong`, `res=` is in **degrees**. For all projected modes, `res=` is
in **metres**. Set `res=0` to skip region setup (e.g. if you will import
a raster and align the region to it with `g.region raster=...`).

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `body=` | mars | Planetary body |
| `location=` | (body name) | New GRASS location name |
| `projection=` | latlong | Map projection |
| `res=` | 0.01 | Resolution (degrees or metres) |
| `lon_0=` | 0.0 | Central / reference longitude |
| `lat_0=` | 0.0 | Centre latitude (for lcc/laea/ortho) |
| `lat_1=` | 30.0 | First standard parallel (lcc) |
| `lat_2=` | 60.0 | Second standard parallel (lcc) |
| `semi_major=` | | Custom equatorial radius in metres |
| `semi_minor=` | | Custom polar radius (default = semi_major) |
| `-p` | | Print PROJ.4 string and exit (dry run, no GRASS needed) |

## EXAMPLES

Mars location, geographic lat/lon at 0.01° (≈ 600 m at equator):

```sh
p.location.create body=mars projection=latlong res=0.01
```

Moon, simple cylindrical at 500 m/pixel (typical global mosaic):

```sh
p.location.create body=moon projection=eqc res=500 location=moon_eqc
```

Mars, north polar stereographic at 1 km for OMEGA north cap analysis:

```sh
p.location.create body=mars projection=north_stereo res=1000 location=mars_nps
```

Venus custom — large-scale regional sinusoidal map:

```sh
p.location.create body=venus projection=sinu res=2000 lon_0=0 location=venus_sinu
```

Dry-run: print PROJ.4 without creating anything:

```sh
p.location.create -p body=titan projection=lcc lat_1=30 lat_2=60
```

## NOTES

- After creation, switch to the new location with `g.mapset -c mapset=PERMANENT location=<name>`.
- If you will import a specific instrument raster (e.g. via p.in.archive), use `res=0` then `g.region raster=<imported_map>` to align exactly to that product.
- The default region covers the full planetary surface (or full hemisphere for polar projections). Import a regional scene, then `g.region raster=<scene>` to zoom in.
- The `eqc` projection with `lat_ts=0` is the IAU/PDS standard "simple cylindrical" (also called equirectangular at 0° standard parallel), as used by USGS Astropedia and CTX global mosaics.

## SEE ALSO

*g.proj*, *g.region*, *p.in.archive*, *p.target.info*
