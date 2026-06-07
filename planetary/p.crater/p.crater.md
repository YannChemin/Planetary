## DESCRIPTION

*p.crater* applies impact-crater scaling laws (Melosh 1989 ch. 7,
Holsapple 1993, Pike 1980, Schmidt & Holsapple 1982) to **each crater
rim polygon** of an input GRASS vector map.

Compared to the legacy *r.crater* addon (which operated only on raster
maps of pre-computed impactor parameters), *p.crater* adds:

- a **built-in planetary body database** of 42 Solar System bodies
  spanning terrestrial planets, all major outer-planet moons (the
  Galilean moons of Jupiter; Mimas/Enceladus/Tethys/Dione/Rhea/
  Titan/Hyperion/Iapetus/Phoebe at Saturn; Miranda/Ariel/Umbriel/
  Titania/Oberon at Uranus; Triton at Neptune), the Martian moons
  Phobos and Deimos, all six IAU-recognized dwarf planets (Pluto,
  Charon, Ceres, Eris, Haumea, Makemake), candidate dwarf planets
  and large TNOs (Gonggong, Quaoar, Sedna, Orcus, Salacia), major
  visited asteroids (Vesta, Pallas, Hygiea, Psyche, Lutetia,
  Mathilde, Eros, Itokawa, Bennu, Ryugu) and the comet 67P. Each
  record supplies the surface gravity, bulk density, typical
  surface (target) density and dominant Gault-scaling target type;
- a special **`body=custom`** option for bodies not in the database;
  when selected, the user must supply `gravity=`, `target_density=`
  and `target_type=` explicitly;
- **vector polygon input** - the equivalent-circle diameter
  D_eq = 2 sqrt(A / π) is computed per feature directly from the
  polygon geometry;
- **optional surface and subsurface density rasters**, each with a
  representative thickness, used to compute a depth-weighted effective
  target density per crater (Melosh 1989 §5.4 - excavation depth ≈
  Dat / 3);
- **optional surface material-type raster** (1/2/3) sampled at the
  crater centroid;
- **optional DEM** sampled at the crater centroid and along the rim
  vertices to measure the crater depth directly (mean rim elevation
  minus centre elevation);
- **multi-method evaluation**: Pi-, Gault- and Yield-scaling are all
  computed and stored as separate attribute columns;
- **simple/complex transition correction** (Pike 1980) to convert the
  apparent transient diameter Dat to the final crater diameter Df;
- **depth prediction** with the Pike (1977/1980) depth-to-diameter
  ratio, for cross-validation against the DEM measurement.

Both directions are supported:

- **Forward** (default if `impactor_diameter` is set): given an
  impactor size, compute Dat, Df, depth and energy.
- **Backward** (`-b` flag, also default if `impactor_diameter` is
  omitted): given the observed crater rim diameter, compute the
  projectile diameter that produced it, plus the kinetic energy and
  TNT-equivalent yield.

### Impactor-points synthesis mode

A second input mode is selected by giving `impactors=` (a POINT
vector) instead of `input=` (a POLYGON vector). In this mode the
module operates **forward only** (the impactor properties come from
the vector attributes per-point) and a DEM (`dem=`) is required so
the elevation, local slope and aspect can be sampled at each impact
site.

For each input point, five attribute columns are read (column names
are themselves options so the user can map them onto any existing
schema):

| Option           | Default       | Meaning                                |
|------------------|---------------|----------------------------------------|
| `col_velocity`   | `velocity`    | Impactor velocity [m/s]                |
| `col_angle`      | `impact_angle`| Impact angle from local DEM surface [deg, 90 = normal] |
| `col_azimuth`    | `azimuth`     | Downrange azimuth [deg, 0 = north, clockwise] |
| `col_density`    | `density`     | Impactor density (unit set by `density_unit=`) |
| `col_diameter`   | `diameter`    | Impactor diameter [m, spheroid]        |
| `density_unit`   | `kg_m3`       | Either `kg_m3` or `g_cm3`              |

Output is a **new** vector containing one or more polygon features
per input impactor:

- **Circle** when the impact angle is >= 30° from the local surface
  (near-normal impact)
- **Ellipse** when 5° <= angle < 30° (oblique impact). The major
  axis is aligned with the downrange azimuth and the minor/major
  ratio follows `(b/a) = sin(theta)^(1/3)` (Pierazzo & Melosh 2000)
- **Ricochet chain** when angle < 5° (grazing impact): the primary
  ellipse is followed by up to four bounce craters along the
  downrange direction, with hop distance ~ 4 × the previous crater
  diameter, velocity attenuated by 0.5 per bounce, impactor size
  attenuated by 0.6 per bounce (Schultz & Gault 1990 morphology),
  until the impactor drops below 1 m or 100 m/s

The output attribute table records, per polygon: `parent_id` (the
originating point), `bounce` (0 for primary, 1+ for ricochets),
`kind` (`circle`/`ellipse`), `D_major`, `D_minor`, `azimuth_deg`,
`theta_local_deg`, `Df_pi`, `depth_pred`, `proj_L_m`, `V_m_s`,
`kinetic_J`, `tnt_kt`, `elev_m`, `slope_deg`, `aspect_deg`.

## OUTPUT ATTRIBUTES

The input geometry is copied unchanged to the output vector. The
attribute table receives the following DOUBLE PRECISION columns:

| Column       | Units    | Meaning                                            |
|--------------|----------|----------------------------------------------------|
| `D_eq`       | m        | Equivalent-circle diameter from polygon area       |
| `Dat_pi`     | m        | Apparent transient diameter, Pi-scaling            |
| `Dat_gault`  | m        | Apparent transient diameter, Gault scaling         |
| `Dat_yield`  | m        | Apparent transient diameter, Yield scaling         |
| `proj_pi`    | m        | Estimated impactor diameter, Pi-scaling            |
| `proj_gault` | m        | Estimated impactor diameter, Gault scaling         |
| `proj_yield` | m        | Estimated impactor diameter, Yield scaling         |
| `kinetic_J`  | J        | Impactor kinetic energy                            |
| `tnt_kt`     | kt TNT   | TNT-equivalent yield (1 kt = 4.184 × 10¹² J)       |
| `Df_pi`      | m        | Final crater diameter (after collapse correction)  |
| `depth_pred` | m        | Predicted depth from Df (Pike depth/diameter)      |
| `depth_dem`  | m        | Measured depth (mean rim − centre, if DEM given)   |
| `dD_ratio`   | -        | Predicted depth/diameter ratio                     |
| `rho_eff`    | kg/m³    | Effective target density actually used             |

## DEPTH/DIAMETER RATIO PER BODY

The `depth_pred` and `dD_ratio` columns are computed from a body-specific
**simple-crater depth/diameter ratio** stored in the planetary-body
database. The complex regime smoothly transitions from this value at
D = Dsc (simple-to-complex transition diameter) down to ~0.05 for very
large craters.

| Body family            | d/D (simple) | Source                                |
|------------------------|--------------|---------------------------------------|
| Moon                   | 0.196        | Pike (1977)                           |
| Mars                   | 0.150        | Pike (1980)                           |
| Mercury                | 0.180        | Pike (1988)                           |
| Venus                  | 0.140        | Schaber et al. (1992)                 |
| Earth                  | 0.130        | Grieve & Pesonen (1992)               |
| Vesta                  | 0.180        | Marchi et al. (2012)                  |
| Ceres                  | 0.170        | Hiesinger et al. (2016) Dawn data     |
| Hyperion               | 0.180        | porous water-ice morphology           |
| Icy moons*             | 0.150        | water-ice rheology (shallower than rock) |
| Small bodies**         | 0.200        | steep bowl morphology of rubble piles |

\* Europa, Ganymede, Callisto, Titan, Mimas, Enceladus, Tethys, Dione,
Rhea, Iapetus, Phoebe, Miranda, Ariel, Umbriel, Titania, Oberon, Triton,
Pluto, Charon, Eris, Haumea, Makemake, Gonggong, Quaoar, Sedna, Orcus,
Salacia, Io (silicate but with extensive volcanic resurfacing).

\** Phobos, Deimos, Pallas, Hygiea, Psyche, Lutetia, Mathilde, Eros,
Itokawa, Bennu, Ryugu, 67P. Vesta uses 0.180 (HED parent body, more
consolidated). Mathilde uses 0.200 (porous C-type morphology).

For `body=custom`, the lunar value 0.196 is used as a defensible default.

### Simple-to-complex transition diameter (Dsc) per body

The simple regime gives a constant d/D; the complex regime smoothly
drops from there with sqrt(Dsc/D). The transition diameter Dsc is now
**measured per body** when published values exist, falling back to
`Dsc = 18 km × g_Moon / g` (Pike 1980 1/g analytic scaling) otherwise:

| Body         | Dsc [km] | Source                              |
|--------------|----------|-------------------------------------|
| Moon         | 18.0     | Pike (1977)                         |
| Mars         |  7.0     | Pike (1980)                         |
| Mercury      | 10.3     | Pike (1988)                         |
| Venus        | 14.0     | Schaber et al. (1992)               |
| Earth        |  3.2     | Pike (1980), continental crust      |
| Vesta        |  7.0     | Marchi et al. (2012)                |
| Ceres        | 10.0     | Hiesinger et al. (2016)             |
| Ganymede     |  3.5     | Schenk (2002)                       |
| Callisto     |  3.5     | Schenk (2002)                       |
| Europa       |  4.0     | Schenk (2002)                       |
| Titan        |  3.0     | Wood et al. (2010) Cassini RADAR    |
| Io           |  5.0     | estimate (silicate analogue)        |
| Other bodies | (1/g)    | 18 km × g_Moon/g analytic fallback  |

Small bodies (Phobos, Deimos, asteroids, comets) keep the 1/g
fallback which yields enormous Dsc, so every crater stays in the
simple regime — physically appropriate since their cratering is
strength-controlled, not gravity-controlled.

### Spatial d/D variation via a raster

For heterogeneous surfaces - e.g. Mars polar ice caps embedded in
basaltic plains, or a Vesta-Hed boundary - supply a raster of
per-pixel d/D values via `dd_simple_map=`. The module samples it at
each crater centroid (polygon mode) or each impactor location
(synthesis mode) and uses that value, falling back to the scalar
`dd_simple=` and then the body database default if the sample is
missing or out of range.

Override hierarchy (highest wins):

1. **`dd_simple_map=`** raster sampled at the crater/impactor centroid
2. **`dd_simple=`** scalar (per-run uniform override)
3. **Body database default** (table above)

Example - Mars surface with a polar-ice carve-out (the polar layer
has d/D = 0.15 like icy moons, the basaltic plains keep Mars'
0.150):

```sh
# Build the d/D raster: 0.15 inside polar lats, body default elsewhere
r.mapcalc "mars_dD = if(y() > 70.0 || y() < -70.0, 0.150, 0.150)"

p.crater -b body=mars dd_simple_map=mars_dD \
    input=mars_rims output=mars_scaled
```

### Overriding the d/D per run

Any of the per-body defaults above can be **overridden on a single
run** with the `dd_simple=` option. Pass a value in (0, 0.5]. This is
useful for:

- testing alternative cratering scaling laws,
- working with bodies not in the database (combine with `body=custom`),
- comparing against published d/D fits from new datasets (e.g.
  Robbins lunar database fits that differ from the 0.196 canonical
  Pike value).

Example - apply a Robbins-style d/D = 0.165 to Mars-mapped craters:

```sh
p.crater -b body=mars dd_simple=0.165 \
    input=hellas_rims output=hellas_scaled \
    impactor_velocity=18000 impactor_density=3000
```

The override applies to every crater in the run (simple regime and
the simple-anchored complex-regime smooth transition).

## NOTES

The polygon area is computed via the shoelace formula on the raw
(x, y) coordinates of the input vector. If the input is in geographic
(lat/lon) coordinates, this gives a meaningless area: **reproject to
a metric local CRS first** (e.g. with *v.proj*, or use the
*p.cam2map* equivalent for planetary projections).

The simple-to-complex transition diameter scales as 1/g; on the Moon
it is ~18 km, on Mars ~7.9 km, on Earth ~3.0 km, on Venus ~3.3 km, on
Mercury ~7.9 km, and on small bodies like Ceres or Vesta several
hundred km. *p.crater* prints this value at run start.

When `surface_density_map` and `subsurface_density_map` are both
provided with their respective thicknesses (`surface_thickness`,
`subsurface_thickness`), the effective target density seen by the
transient excavation cone is computed as

    rho_eff = (h_surf · rho_surf + (d_exc − h_surf) · rho_sub) / d_exc

where d_exc = Dat / 3. If only the surface layer is given, the
subsurface is ignored.

## EXAMPLES

### Backward mode on the Moon

Given a vector `craters_apollo` of mapped Apollo-era impact crater
rims (polygons), estimate the impactor size that made each crater:

```sh
g.region vector=craters_apollo res=100
p.crater -b body=moon \
    input=craters_apollo \
    output=craters_apollo_scaled \
    impactor_velocity=18000 impactor_angle=45 \
    impactor_density=3000
```

### Mars craters with surface/subsurface density layers

A 50-m-thick regolith over basaltic bedrock, with both layers given
as rasters mapped from MOLA gravity and Mars Express MARSIS data:

```sh
p.crater -b body=mars \
    input=hellas_craters \
    output=hellas_craters_scaled \
    surface_density_map=mars_regolith_rho \
    surface_thickness=50 \
    subsurface_density_map=mars_basalt_rho \
    subsurface_thickness=2000 \
    dem=mola_512ppd
```

### Forward mode on Europa - 10 m iron impactor at 25 km/s

```sh
p.crater body=europa \
    input=europa_predicted_sites \
    output=europa_predicted_craters \
    impactor_diameter=10 impactor_velocity=25000 \
    impactor_angle=45 impactor_density=7800
```

### Custom body not in the database

The Centaur 10199 Chariklo with its ring system - no database entry,
all parameters supplied by hand:

```sh
p.crater -b body=custom \
    input=chariklo_craters \
    output=chariklo_craters_scaled \
    gravity=0.052 \
    target_density=1000 \
    target_type=1 \
    impactor_velocity=5000 impactor_angle=45 \
    impactor_density=1000
```

### Synthesis from a point vector of impactor locations

Predict the surface footprint of a swarm of incoming impactors on a
mapped Martian DEM. Each point in `incoming_swarm` has columns
`velocity`, `impact_angle`, `azimuth`, `density`, `diameter`:

```sh
p.crater body=mars \
    impactors=incoming_swarm \
    dem=mola_512ppd \
    output=predicted_craters \
    density_unit=g_cm3
```

If the schema uses different column names, map them explicitly:

```sh
p.crater body=mars \
    impactors=incoming_swarm dem=mola_512ppd \
    output=predicted_craters \
    col_velocity=V_mps col_angle=elev_deg col_azimuth=az_deg \
    col_density=rho_g_cc col_diameter=size_m \
    density_unit=g_cm3
```

A grazing impactor (small `impact_angle`) will produce an elongated
primary ellipse followed by a chain of decreasing-size ricochet
craters along the downrange azimuth; near-normal impactors produce
single circular craters.

## REFERENCES

- Melosh, H. J. (1989). *Impact Cratering: A Geologic Process*.
  Oxford University Press. ISBN 0-19-504284-0.
- Holsapple, K. A. (1993). "The Scaling of Impact Processes in
  Planetary Sciences." *Annual Review of Earth and Planetary
  Sciences*, 21, 333-373.
  [doi:10.1146/annurev.ea.21.050193.002001](https://doi.org/10.1146/annurev.ea.21.050193.002001)
- Schmidt, R. M., & Holsapple, K. A. (1982). "Estimates of crater
  size for large-body impact: Gravity-scaling results." In *Geological
  Implications of Impacts of Large Asteroids and Comets on the Earth*,
  GSA Special Paper 190, 93-102.
- Pike, R. J. (1980). "Control of crater morphology by gravity and
  target type: Mars, Earth, Moon." *Proc. 11th Lunar Planet. Sci.
  Conf.*, 2159-2189.
- Pike, R. J. (1977). "Apparent depth/apparent diameter relations for
  lunar craters." *Proc. 8th Lunar Sci. Conf.*, 3427-3436.
- Gault, D. E. (1974). "Impact cratering." In *A Primer in Lunar
  Geology*, NASA Ames TM X-62359, 137-175.
- Nordyke, M. D. (1962). "An analysis of cratering data from desert
  alluvium." *Journal of Geophysical Research*, 67(5), 1965-1974.
  [doi:10.1029/JZ067i005p01965](https://doi.org/10.1029/JZ067i005p01965)
- Croft, S. K. (1985). "The scaling of complex craters." *Proc. 15th
  Lunar Planet. Sci. Conf., Part 2; J. Geophys. Res.*, 90, C828-C842.
  [doi:10.1029/JB090iS02p0C828](https://doi.org/10.1029/JB090iS02p0C828)
- Kring, D. A. (2007). "The Chicxulub Impact Event and its
  Environmental Consequences at the Cretaceous-Tertiary Boundary."
  *Palaeogeography, Palaeoclimatology, Palaeoecology*, 255(1-2), 4-21.
  [doi:10.1016/j.palaeo.2007.02.037](https://doi.org/10.1016/j.palaeo.2007.02.037)
- Pierazzo, E., & Melosh, H. J. (2000). "Understanding Oblique Impacts
  from Experiments, Observations, and Modeling." *Annual Review of
  Earth and Planetary Sciences*, 28, 141-167.
  [doi:10.1146/annurev.earth.28.1.141](https://doi.org/10.1146/annurev.earth.28.1.141)
- Schultz, P. H., & Gault, D. E. (1990). "Prolonged Global Catastrophes
  from Oblique Impacts." *Geological Society of America Special Papers*,
  247, 239-261.
  [doi:10.1130/SPE247-p239](https://doi.org/10.1130/SPE247-p239)
- Horn, B. K. P. (1981). "Hill Shading and the Reflectance Map."
  *Proceedings of the IEEE*, 69(1), 14-47.
  [doi:10.1109/PROC.1981.11918](https://doi.org/10.1109/PROC.1981.11918)
- Pike, R. J. (1988). "Geomorphology of impact craters on Mercury."
  In *Mercury* (eds. Vilas, Chapman, Matthews), Univ. of Arizona Press,
  pp. 165-273. (Mercury d/D ~ 0.18.)
- Schaber, G. G., Strom, R. G., Moore, H. J., et al. (1992).
  "Geology and distribution of impact craters on Venus: What are they
  telling us?" *Journal of Geophysical Research*, 97(E8), 13257-13301.
  [doi:10.1029/92JE01246](https://doi.org/10.1029/92JE01246)
- Grieve, R. A. F., & Pesonen, L. J. (1992). "The terrestrial impact
  cratering record." *Tectonophysics*, 216(1-2), 1-30.
  [doi:10.1016/0040-1951(92)90152-V](https://doi.org/10.1016/0040-1951(92)90152-V)
- Marchi, S., McSween, H. Y., O'Brien, D. P., et al. (2012). "The
  Violent Collisional History of Asteroid 4 Vesta." *Science*, 336(6082),
  690-694.
  [doi:10.1126/science.1218757](https://doi.org/10.1126/science.1218757)
- Hiesinger, H., Marchi, S., Schmedemann, N., et al. (2016). "Cratering
  on Ceres: Implications for its crust and evolution." *Science*,
  353(6303), aaf4759.
  [doi:10.1126/science.aaf4759](https://doi.org/10.1126/science.aaf4759)
- Archinal, B. A., A'Hearn, M. F., Bowell, E., et al. (2018). "Report
  of the IAU Working Group on Cartographic Coordinates and Rotational
  Elements: 2015." *Celestial Mechanics and Dynamical Astronomy*,
  130(3), 22.
  [doi:10.1007/s10569-017-9805-5](https://doi.org/10.1007/s10569-017-9805-5)

## SEE ALSO

*[p.crater.freq](p.crater.freq.md) — crater size-frequency dating,
[p.target.info](p.target.info.md) — body radii/gravity from SPICE,
[r.crater](r.crater.md) — original raster-only addon this module
extends*

## AUTHOR

Yann Chemin (dr.yann.chemin@gmail.com)

## LICENSE

The Unlicense ([https://unlicense.org](https://unlicense.org)) -
this module is released into the public domain.
