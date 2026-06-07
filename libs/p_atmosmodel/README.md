# p_atmosmodel Library

Atmospheric radiative transfer models for planetary image correction.

## Overview

The p_atmosmodel library implements four Chandrasekhar-based atmospheric radiative transfer models to correct planetary images for atmospheric scattering and transmission. These models are essential for data acquired through thick atmospheres (Venus, early Mars, Earth) or where aerosol optical properties are well-characterized.

## Supported Models

| Model | Type | Use Case | Key Parameters |
|---|---|---|---|
| Isotropic1 | Single-scattering | Thin aerosol layers, single scattering dominates | `tau`, `wha` |
| Isotropic2 | Multiple-scattering | Moderate aerosol, multiple orders included | `tau`, `wha`, `hga`, `hnorm` |
| Anisotropic1 | Single-scattering with phase | Directional aerosol scattering, single order | `tau`, `wha`, `hga` |
| Anisotropic2 | Multiple-scattering with phase | Complex atmospheres, anisotropic aerosols | `tau`, `wha`, `hga`, `bha`, `hnorm` |

## Parameters

All models use the following parameters:

| Symbol | Name | Range | Description |
|---|---|---|---|
| τ (tau) | Optical depth | 0–10 | Vertical aerosol optical thickness at reference wavelength |
| ω (wha) | Single-scatter albedo | 0–1 | Fraction of scattered vs. absorbed photons per aerosol particle |
| g (hga) | Asymmetry parameter | −1–1 | Henyey-Greenstein phase function asymmetry (0=isotropic, >0=forward) |
| b (bha) | Backscatter fraction | 0–1 | Fraction of scattering directed backward (Anisotropic2 only) |
| h_norm (hnorm) | Normalization height | 0–100 km | Height above surface where Chandrasekhar H-function is normalized |

## API

### Model Evaluation

```c
double atmospheric_model(const char *model_name,
                         double tau,      /* optical depth */
                         double wha,      /* single-scatter albedo */
                         double incidence, /* degrees */
                         double emission,  /* degrees */
                         double phase,     /* degrees */
                         const double *extra_params);
```

### Cached H-Function Computation

```c
typedef struct {
    char model_name[32];
    double tau, wha, hga, bha, hnorm;
    double *en_cache;  /* Normalized E_n integrals */
    double *ei_cache;  /* Normalized E_i integrals */
    int cache_size;
} AtmosphericModel;

AtmosphericModel *p_atmosmodel_create(const char *model_name);
void p_atmosmodel_set_params(AtmosphericModel *m,
                              double tau, double wha, double hga,
                              double bha, double hnorm);
double p_atmosmodel_evaluate(AtmosphericModel *m,
                              double incidence, double emission, double phase);
void p_atmosmodel_free(AtmosphericModel *m);
```

## Radiative Transfer Theory

### Single-Scattering Approximation (Isotropic1)

Radiance leaving surface after atmospheric passage:

$$L = I(\mu_0) \mu_0 \rho + I_s(\mu_0, \mu) t(\mu)$$

where:
- $I(\mu_0) = e^{-\tau/\mu_0}$ = direct transmission
- $I_s$ = scattered light integral
- $t(\mu) = e^{-\tau/\mu}$ = emergence transmission
- $\rho$ = surface reflectance

### Multiple-Scattering (Isotropic2, Anisotropic1/2)

Uses Chandrasekhar's H-function method with iterative solution of:

$$H(\mu) = 1 + \frac{\omega}{2} \int_0^1 \frac{H(\mu') p(\mu, \mu')}{\mu + \mu'} d\mu'$$

where $p(\mu, \mu')$ is the aerosol phase function. Numerical integration via Gaussian quadrature with caching of $H(\mu_i)$ values at standard angles.

### Anisotropic Phase Functions

Henyey-Greenstein phase function:

$$p(g) = \frac{1 - g^2}{(1 + g^2 - 2g\cos\theta)^{3/2}}$$

Backscatter fraction parameterization:

$$b(g) = \frac{b_{ha} (1 + g^2)}{2(1 - 2g\cos(90°))^{3/2}}$$

## Compilation

### Standalone Compilation

```bash
gcc -DP_ATMOSMODEL_STANDALONE -fopenmp -I. \
    -c p_atmosmodel.c -o p_atmosmodel.o
gcc -o test_atmosmodel test_atmosmodel.c p_atmosmodel.o -lm
```

### Integration with GRASS

Compiled as part of p.atcorr.hapke module via Makefile dependency.

## Usage Example

Correct CRISM image for Martian dust (τ≈0.5, ω≈0.9, g≈0.65):

```c
AtmosphericModel *atm = p_atmosmodel_create("Anisotropic2");
p_atmosmodel_set_params(atm, 0.5, 0.9, 0.65, 0.15, 0.0);

double i_f_measured = 0.08;
double i_f_corrected = p_atmosmodel_evaluate(atm, 45.0, 30.0, 75.0);
double i_f_surface = i_f_measured / i_f_corrected;

p_atmosmodel_free(atm);
```

## Scientific References

- Chandrasekhar, S. (1960). *Radiative Transfer*. Oxford University Press. Reprinted by Dover (2013). ISBN 0-486-60590-6.
- Hapke, B. W. (1993). *Theory of Reflectance and Emittance Spectroscopy*. Cambridge University Press. ISBN 0-521-30789-9.
- Ahmad, Z., & Franz, B. A. (1984). "Atmospheric Effects on Remote Sensing." *IEEE Transactions on Geoscience and Remote Sensing*, GE-22(2), 159–163.
- Tomasko, M. G., Karkoschka, E., Zarnecki, J. C., et al. (1997). "A model of the structure of the Venusian atmosphere from surface to 100 km altitude." *Advances in Space Research*, 19(8), 1123–1127.

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
