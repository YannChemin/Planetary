## DESCRIPTION

*p.iirs.correct* applies the thermal emission correction algorithm of
Verma, Chauhan & Chauhan (2022) to a Chandrayaan-2 Imaging Infrared
Spectrometer (IIRS) Level-2 radiance imagery group, producing
thermally corrected reflectance and a per-pixel surface temperature map.

The algorithm:

1. Estimates surface temperature per pixel by inverting the Planck function
   in the 4500–4874 nm thermal window (configurable via `wave_lo=`/`wave_hi=`).
2. Computes the blackbody thermal emission spectrum at that temperature.
3. Subtracts the thermal component from the measured radiance and divides by
   the solar irradiance to obtain reflectance: `R = π(L − ε·B_T) / F_solar`.
4. Applies empirical per-band correction coefficients (Verma et al. 2022,
   Table S1, 240 values for the 800–5000 nm output window).
5. Smooths each spatial pixel's spectrum with a 3-point moving average.

The first 7 and last 2 of the 256 IIRS bands are discarded (known
detector-edge artefacts). Only the 800–5000 nm output window is written.

## INPUT DATA

IIRS Level-2 data is distributed by the Indian Space Research Organisation
(ISRO) through the ISSDC portal (issdc.gov.in) and requires user registration.
Files are in ENVI format. Import one cube with *r.in.gdal*, then collect all
256 bands into an imagery group with *i.group*:

```sh
# Split all 256 bands on import
r.in.gdal -r input=ch2_iirs_nbl_20191012T0722_v01.img output=iirs_rad

# Collect into group (bands are named iirs_rad.1 .. iirs_rad.256)
i.group group=iirs_rad \
    input=$(g.list type=raster pattern="iirs_rad.*" separator=,)
```

## OUTPUT FILES

| Map | Content |
|---|---|
| `<output>.b001` … `<output>.bNNN` | Thermally-corrected reflectance, one map per band (800–5000 nm) |
| `<output>.temp` | Per-pixel surface temperature (K), coloured `bcyr` |

When `-g` is set, all reflectance maps are also collected into an imagery
group named `<output>`.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `input=` | required | Imagery group with 256 IIRS L2 radiance bands |
| `output=` | required | Output raster base name |
| `solar_flux=` | bundled | Two-column ASCII file: wavelength_nm, irradiance (256 rows) |
| `emissivity=` | 0.95 | Surface emissivity assumed constant (lunar regolith default) |
| `wave_lo=` | 4500 | Lower bound (nm) of the temperature retrieval window |
| `wave_hi=` | 4874 | Upper bound (nm) of the temperature retrieval window |

## FLAGS

| Flag | Effect |
|---|---|
| `-g` | Create output imagery group containing all reflectance bands |

## NOTES

- The default solar irradiance spectrum bundled in the module is the
  two-column ASCII file distributed with the CH2IIRS QGIS plugin
  (Verma et al. 2022), covering all 256 IIRS bands from 712 to 5010 nm.
- Computation is fully vectorised with NumPy; processing time scales linearly
  with spatial extent, not with the per-pixel Python loop of the reference
  implementation.
- The emissivity is treated as spatially constant. For heterogeneous
  surfaces, run the module on sub-regions with different `emissivity=` values.
- The temperature map (`<output>.temp`) is the mean Planck-inversion
  temperature across the `wave_lo`–`wave_hi` window, not a kinetic temperature.

## EXAMPLE

```sh
# Import and group a full IIRS scene
r.in.gdal -r input=ch2_iirs_nbl_20191012T0722_orbit_00399_v01.img \
    output=iirs_rad
i.group group=iirs_rad \
    input=$(g.list type=raster pattern="iirs_rad.*" separator=,)

# Thermal correction with defaults (emissivity=0.95, bundled solar flux)
p.iirs.correct input=iirs_rad output=iirs_corr -g

# Inspect temperature
r.univar map=iirs_corr.temp
d.rast iirs_corr.temp

# Use 3 µm hydration feature: band depth around 2800 nm in the output group
p.mineral.indices input=iirs_corr body=moon indices=bd2800
```

## REFERENCES

Verma, P.A., Chauhan, M., & Chauhan, P. (2022). Lunar surface temperature
estimation and thermal emission correction using Chandrayaan-2 imaging
infrared spectrometer data for H₂O & OH detection using 3 µm hydration
feature. *Icarus*, 383, 115075.
<https://doi.org/10.1016/j.icarus.2022.115075>

## SEE ALSO

*p.in.archive*, *p.mineral.indices*, *p.spec.pca*, *i.group*, *r.in.gdal*

## AUTHOR

Yann Chemin
