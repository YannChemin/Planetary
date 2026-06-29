## DESCRIPTION

*p.mineral.indices* computes body-specific planetary mineral spectral indices
from multi-band raster imagery. The `body=` option selects the index set;
generic indices (olivine, pyroxene, TiO2/FeO Clementine ratios) are available
for all bodies.

Input bands must be named `<input>.1`, `<input>.2`, … (the standard GRASS
imagery group convention used by p.in.archive, p.in.isis, p.spec.pca, etc.).
Supply a `wavelengths=` CSV (one wavelength in µm per line) to correctly
map band numbers to physical wavelengths; without it a 10 nm fallback grid
starting at 0.4 µm is assumed, which is only valid for band-ratio indices.

### Index algorithm types

| Type | Formula | Use case |
|---|---|---|
| Band depth (BD) | `1 − R(λ_c) / interpolated_continuum` | Absorption feature strength |
| Band ratio | `R(λ_1) / R(λ_2)` | Compositional colour index |
| Integrated BD (IBD) | `Σ BD(λ_i)` over a wavelength range | Broad absorption intensity (M3) |
| Spectral slope | `(R_hi/R_lo − 1) / (λ_hi − λ_lo)` | Space-weathering / maturity |

### body=mars (default)

| `index=` | Formula | Sensitivity |
|---|---|---|
| `olivine` | BD at 1.05 µm | Mg-rich olivine absorption |
| `pyroxene` | BD at 2.0 µm | Low- and high-Ca pyroxene |
| `tio2` | R(415nm)/R(750nm) | TiO2 content (Lucey et al. 2000) |
| `feo` | R(950nm)/R(750nm) | FeO content (Lucey et al. 2000) |
| `mafic` | IBD(1.05µm) + IBD(2.0µm) | Combined mafic mineral content |

### body=moon — M3 (Clark et al. 2011, J. Geophys. Res.)

| `index=` | Formula | Sensitivity |
|---|---|---|
| `ibd1000` | IBD 0.82–1.19 µm (continuum 0.749→1.309 µm) | Olivine + LCP/HCP absorption |
| `ibd2000` | IBD 1.66–2.50 µm (continuum 1.579→2.499 µm) | HCP pyroxene absorption |
| `r1580_1250` | R(1.58µm)/R(1.25µm) | OH/H2O overtone — hydroxyl minerals |
| `bd2800` | BD at 2.8 µm | OH/H2O — lunar swirls and ice |

Plus all body=mars indices.

### body=mercury — MDIS (Denevi et al. 2009, 2016)

| `index=` | Formula | Sensitivity |
|---|---|---|
| `r749_433` | R(749nm)/R(433nm) | Maturity / space weathering |
| `r996_749` | R(996nm)/R(749nm) | Mafic absorption (near-IR/visible) |
| `spec_slope` | (R(996)/R(433)−1) / 0.563 µm⁻¹ | Spectral slope — colour unit mapping |

Plus all body=mars indices.

### body=titan — VIMS (Soderblom et al. 2007, Icarus 194)

| `index=` | Formula | Sensitivity |
|---|---|---|
| `r500_200` | R(5.0µm)/R(2.0µm) | Dark tholin-rich vs water ice |
| `r280_200` | R(2.8µm)/R(2.0µm) | Water ice (deep 2.0µm absorption) |
| `r159_127` | R(1.59µm)/R(1.27µm) | Hydrocarbon ice species |

Plus all body=mars indices.

### body=venus — VIRTIS (Meadows & Crisp 1996; Bézard et al. 2009)

| `index=` | Formula | Sensitivity |
|---|---|---|
| `r1740_1300` | R(1.74µm)/R(1.30µm) | 1.74µm surface emission window |
| `r1180_1100` | R(1.18µm)/R(1.10µm) | 1.18µm surface emission window |
| `bd2300` | BD at 2.3 µm | CO2/surface 2.3µm window |

Plus all body=mars indices.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `body=` | mars | Target body (selects the index set) |
| `input=` | required | Base name of input band rasters |
| `output=` | required | Output raster (index values) |
| `index=` | required | Spectral index to compute |
| `wavelengths=` | | CSV of per-band wavelengths in µm (one per line) |

## EXAMPLES

Mars olivine band depth from a CRISM FRT imagery group:

```sh
p.mineral.indices body=mars input=crism_frt output=crism_olivine \
    index=olivine wavelengths=crism_vnir_wavelengths.csv
```

Moon IBD1000 from M3 Level 2 imagery group:

```sh
p.mineral.indices body=moon input=m3_L2 output=m3_ibd1000 \
    index=ibd1000 wavelengths=m3_wavelengths.csv
```

Mercury colour ratio from MDIS 8-filter cube:

```sh
p.mineral.indices body=mercury input=mdis_8f output=mdis_maturity \
    index=r749_433 wavelengths=mdis_wavelengths.csv
```

Titan dark-material index from VIMS cube:

```sh
p.mineral.indices body=titan input=vims_cube output=titan_dark \
    index=r500_200 wavelengths=vims_wavelengths.csv
```

## NOTES

- The `wavelengths=` CSV must have **one wavelength in µm per line** (no header,
  no commas). Band order must match the `<input>.N` band numbering.
- For IBD indices, the continuum is a straight line between the two shoulder
  wavelengths; individual band depths are summed with equal weights. For
  accurately spaced M3 data this matches the Clark 2011 formulation.
- The spectral slope (`spec_slope`) is normalised to R at the lower wavelength,
  expressed per µm. Positive = red slope (space-weathered/mature regolith).

## REFERENCES

- Clark, R.N. et al. (2011). Detection and mapping of hydroxyls and water on
  the Moon using M3. *J. Geophys. Res.* 116:E00G16.
- Denevi, B.W. et al. (2009). The evolution of Mercury's crust. *Science* 324:613.
- Lucey, P.G. et al. (2000). Imaging of lunar surface maturity. *J. Geophys. Res.*
  105(E8):20377–20386.
- Meadows, V.S. & Crisp, D. (1996). Ground-based near-infrared observations of
  the Venus nightside. *J. Geophys. Res.* 101(E2):4595–4622.
- Soderblom, L.A. et al. (2007). Correlations between Cassini VIMS spectra and
  RADAR SAR images. *Icarus* 194:265–277.

## SEE ALSO

*p.spec.pca*, *p.spec.class*, *p.spec.library*, *p.spectral.planet*, *p.matter.bands*
