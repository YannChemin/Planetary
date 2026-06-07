## DESCRIPTION

**p.illumination.shadow** computes shadow frequency, grazing-light frequency,
and maximum solar elevation over a full planetary cycle. It complements
*p.illumination.sunfraction* by characterising the shadow side of the
illumination picture.

### Outputs

- **Shadow frequency**: fraction of timesteps each pixel is in shadow.
  Pixels above `shadow_threshold` (default 0.70 = shadowed > 70% of the
  time) are flagged as hazardous cold traps.
- **Grazing-light frequency**: fraction of illuminated timesteps where solar
  elevation is below `grazing_threshold` (default 5°). Grazing light creates
  long shadows and extreme thermal gradients.
- **Maximum solar elevation**: peak solar elevation across the full cycle.

### Methodology (Turchinskaya & Slyuta 2024)

Same simulation engine as *p.illumination.sunfraction*: sub-solar position
→ solar elevation/azimuth → shadow mask via `sunmask_module` → aggregation.
Shadow frequency = 1 − illumination fraction.

The approach follows Turchinskaya & Slyuta (2024), who used 1-h timesteps
over the full ~18.6-year lunar nutation cycle for Luna-27 candidate sites.

### Ephemeris model

The sub-solar point at each timestep is computed with the `ephemeris=`
cascade:

- **spice** — true NAIF positions for any body with kernels; requires
  *p.in.spice* + *p.spice.config* (and `libcspice.so`).
- **meeus** — self-contained truncated analytic ephemeris (Earth's Moon
  only); no external kernels.
- **analytic** — low-fidelity single-sine toy model (any body).

**auto** (default) picks the best available. The start epoch comes from
`start_epoch=`, the body JSON, or defaults to J2000.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dem` | *required* | Input DEM raster (metres) |
| `body` | *required* | Body descriptor JSON |
| `nsteps` | 36 | Number of time steps |
| `shadow_threshold` | 0.70 | Shadow frequency threshold for hazard flag (0–1) |
| `grazing_threshold` | 5.0 | Solar elevation for grazing-light flag (degrees) |
| `prefix` | `shadow` | Output map name prefix |
| `sunmask_module` | `p.sunmask` | Shadow-mask module to call |
| `ephemeris` | `auto` | Sub-solar model: `auto`, `spice`, `meeus`, `analytic` (cascade spice→meeus→analytic) |
| `start_epoch` | J2000 | UTC start epoch (ISO-8601) for the real ephemeris |

## OUTPUT MAPS

| Map | Description |
|-----|-------------|
| `<prefix>_frequency` | Shadow frequency [0–1] |
| `<prefix>_hazard_mask` | 1 where shadow frequency exceeds threshold |
| `<prefix>_grazing_freq` | Fraction of illuminated steps with grazing light |
| `<prefix>_max_elevation` | Maximum solar elevation in the cycle (degrees) |

## EXAMPLES

```bash
p.illumination.shadow dem=lola_5m body=bodies/moon.json \
                      nsteps=36 shadow_threshold=0.65 prefix=shadow
```

## NOTES

The timestep and solar-position method must match those used in *p.illumination.sunfraction* so that shadow frequency and illumination fraction sum to a consistent picture. For polar sites, convergence of the shadow-frequency estimate requires at least 1000 timesteps; fewer steps are acceptable for preview maps but not publication-quality results.

## SEE ALSO

*[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.sunmask](p.sunmask.md),
[r.series](https://grass.osgeo.org/grass-stable/manuals/r.series.html)*

## REFERENCES

- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011

## AUTHOR

Yann Chemin
