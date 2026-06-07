## DESCRIPTION

**p.in.ancillary** imports ancillary planetary data layers — thermal inertia,
surface temperature, albedo, elemental abundances (FeO, TiO2, OMAT), mineral
maps, crustal thickness, gravity gradients, water equivalent hydrogen (WEH),
volatile proxies, impact crater databases, and geologic unit maps — into
GRASS GIS.

The `type=` parameter selects appropriate scale factors and import strategies
so common datasets arrive in standard physical units without manual
configuration.

### Crater database import

When `type=craters` the input is a Robbins & Hynek (2012) or similar CSV
file. Columns used (1-indexed): 1 (crater ID), 2 (latitude), 3 (longitude),
4 (diameter km), 5 (depth km), 6 (morphology). Output is a GRASS vector point
map with attributes.

### WEH and volatile proxies (Turchinskaya & Slyuta 2024)

WEH data (e.g., from LRO/LEND neutron spectrometry) is imported as a raster.
Turchinskaya & Slyuta (2024) rank Luna-27 candidate sites by WEH
concentration (0.1–0.2 wt% and 0.2–0.3 wt%) as the primary scientific
criterion.

## PARAMETERS

| Parameter | Default | Description |
|-----------|---------|-------------|
| `input` | *required* | Input file (raster, vector, or crater CSV) |
| `output` | *required* | Output GRASS map name |
| `type` | *required* | Data type: `thermal_inertia`, `temperature`, `albedo`, `feo`, `tio2`, `omat`, `mineralogy`, `crust_thickness`, `gravity_gradient`, `weh`, `volatile_proxy`, `craters`, `geology_units`, `custom` |
| `scale` | 0 (auto) | Multiplicative scale factor; 0 = auto from type defaults |
| `offset` | 0 | Additive offset applied after scale |
| `resample` | `bilinear_f` | Resampling method for reprojection |
| `memory` | 300 | Maximum memory in MB |

## FLAGS

| Flag | Description |
|------|-------------|
| `-n` | Normalise output to [0, 1] |
| `-r` | Set computational region to match the imported map |

## EXAMPLES

```bash
# Import Clementine-derived FeO abundance
p.in.ancillary input=lunar_feo.tif output=feo type=feo

# Import LRO/LEND WEH raster, normalise to [0,1]
p.in.ancillary input=lend_weh.tif output=weh type=weh -n

# Import Robbins crater database
p.in.ancillary input=Lunar_Impact_Crater_Database_v08Sep2015.csv \
               output=craters type=craters
```

## NOTES

Scale factors and physical units for each `type=` value are hardcoded to match the canonical planetary dataset distributions (LRO, MESSENGER, Dawn, Cassini). Verify the applied conversion by checking the output map's history metadata (`r.support -h`) after import. The crater-database import path creates a GRASS vector map; the spatial index is built automatically.

## SEE ALSO

*[p.in.pds](p.in.pds.md),
[p.in.dem](p.in.dem.md),
[r.import](https://grass.osgeo.org/grass-stable/manuals/r.import.html)*

## REFERENCES

- Robbins, S.J. & Hynek, B.M. (2012) A new global database of Mars impact
  craters ≥1 km. *Journal of Geophysical Research: Planets* 117, E05004.
  [crater database format]
- Lucey, P.G. et al. (2000) Lunar iron and titanium abundance algorithms
  based on final processing of Clementine UV-VIS images. *Journal of
  Geophysical Research: Planets* 105(E8), 20297–20305.
  doi:10.1029/1999JE001117
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011 *(WEH as primary science criterion)*

## AUTHOR

Yann Chemin
