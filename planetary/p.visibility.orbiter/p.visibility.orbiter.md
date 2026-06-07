## DESCRIPTION

**p.visibility.orbiter** computes the fraction of time each surface pixel has
line-of-sight contact with a relay orbiter on a circular Keplerian orbit.

### Algorithm

1. A Keplerian ground track is simulated for `norbits` complete orbits.
   At each of `norbits × steps_per_orbit` sample points the sub-satellite
   geographic coordinates are computed analytically.
2. The orbiter's elevation and azimuth at the region centre are computed from
   the sub-satellite position using spherical trigonometry.
3. The terrain horizon angle at that azimuth is linearly interpolated from
   pre-computed *r.horizon* maps.
4. A pixel records contact if orbiter elevation > horizon elevation +
   `min_elev_deg`.
5. Contact fraction = contact samples / total samples.

### Keplerian orbit geometry

For a circular orbit at altitude *h* above a spherical body of radius *R*,
the orbital period is:

```
T = 2π × sqrt((R + h)³ / (G × M))
```

The sub-satellite latitude φ_s and longitude λ_s are computed from the
orbital inclination, RAAN, and elapsed time, projected onto the body's
rotating reference frame.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `body` | *required* | Body descriptor JSON (provides G×M, radius) |
| `altitude_km` | 100 | Orbiter altitude in km |
| `inclination` | 90 | Orbital inclination in degrees (90 = polar) |
| `norbits` | 14 | Number of complete orbits to simulate |
| `steps_per_orbit` | 72 | Sample points per orbit |
| `min_elev_deg` | 5.0 | Minimum orbiter elevation above local horizon (degrees) |
| `horizon_step` | 10.0 | Angular step for pre-computed horizon maps (degrees) |
| `prefix` | `orbiter` | Output map name prefix |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_contact_fraction` | Orbiter contact fraction [0–1] |

## EXAMPLES

```bash
# Luna-26 relay orbit (60–80 km altitude, polar)
p.visibility.orbiter dem=lola_5m body=bodies/moon.json \
                     altitude_km=70 inclination=90 norbits=28 \
                     prefix=luna26_contact
```

## PERFORMANCE

The 16-azimuth horizon precompute dominates wall-clock on dense polar DEMs.
Set `HORIZON_BACKEND=gpu` in the environment to swap the internal *r.horizon*
call for *[p.horizon.gpu](p.horizon.gpu.md)* (OpenCL); on a Quadro P1000
this drops the 3000×3000@5 m precompute from ~50 min to seconds. The GPU
backend requires a conformal CRS (UTM, polar stereographic, Lambert Conformal
Conic, Mercator and variants) and produces geometrically correct horizons
(numerically distinct from *r.horizon* on polar DEMs).

```bash
HORIZON_BACKEND=gpu grass ~/grassdata/Moon_SouthPole_5m/mapset --exec \
    p.visibility.orbiter dem=lola_5m body=bodies/moon.json \
        altitude_km=70 inclination=90 norbits=28 prefix=luna26_contact
```

## NOTES

The Keplerian orbit model does not account for orbital precession, J2 oblateness perturbations, or eclipse interruptions. For low-altitude polar orbiters (< 100 km altitude) J2 precession is significant; contact-fraction estimates are conservative upper bounds in that regime. Increase `steps_per_orbit` to reduce discretisation error at the cost of longer runtime.

## SEE ALSO

*[p.visibility.earth](p.visibility.earth.md),
[p.visibility.los](p.visibility.los.md),
[r.horizon](https://grass.osgeo.org/grass-stable/manuals/r.horizon.html)*

## REFERENCES

- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011
- Bate, R.R., Mueller, D.D. & White, J.E. (1971) *Fundamentals of
  Astrodynamics*. Dover Publications. [Keplerian orbit equations]

## AUTHOR

Yann Chemin
