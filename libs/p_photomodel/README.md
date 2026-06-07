# p_photomodel Library

Photometric models for planetary surface reflectance normalization and albedo estimation.

## Overview

The p_photomodel library implements seven photometric models widely used in planetary science to normalize reflectance data to a standard geometry and estimate intrinsic planetary surface properties from bidirectional reflectance measurements.

## Supported Models

| Model | Abbrev. | Use Case | Key Parameters |
|---|---|---|---|
| Lambert | LAMBERT | Perfectly diffuse surfaces | None (intrinsic albedo = I/F at normal incidence) |
| Lommel-Seeliger | LS | Low-albedo airless bodies (Moon, Mercury) | None (geometric albedo at g=0°) |
| Lunar-Lambert | LL | Lunar regolith | `L` (relative diffuse fraction, 0–1) |
| Minnaert | MINNAERT | Surfaces with wavelength-dependent scattering | `k` (Minnaert exponent, typically 0.5–1.0) |
| Hapke (Hensel) | HAPKEHEN | Regolith with opposition surge, phase-dependent | `w` (albedo), `b` (opposition width, radians), `c` (opposition amplitude) |
| Hapke (Legendre) | HAPKELOG | Regolith with Legendre polynomial phase function | `w`, `hg1`, `hg2` (Henyey-Greenstein coefficients) |
| Lunar-Lambert McEwen | LUNARLAMBERT | Lunar-specific refinement with non-linear mixing | `L` (diffuse fraction), exponential factors |

## API

### Photometric Evaluation

```c
double photometric_model(const char *model_name,
                         double albedo,
                         double incidence,   /* i, degrees */
                         double emission,    /* e, degrees */
                         double phase,       /* g, degrees */
                         const double *params);
```

### Model Parameter Structures

```c
typedef struct {
    char name[32];
    int n_params;
    double *param_values;
} PhotometricParameters;

PhotometricParameters *p_photomodel_create(const char *model_name);
void p_photomodel_set_param(PhotometricParameters *p, int idx, double value);
double p_photomodel_evaluate(PhotometricParameters *p,
                              double incidence, double emission, 
                              double phase);
void p_photomodel_free(PhotometricParameters *p);
```

## Model Equations

### Lambert

Reflectance:
$$\rho(\mu_0, \mu) = A \mu_0$$

where:
- $A$ = intrinsic albedo (bidirectional reflectance at i=0°, e=0°)
- $\mu_0 = \cos(i)$

### Minnaert

$$\rho(\mu_0, \mu) = A \mu_0 \mu^{k-1}$$

where $k$ is the Minnaert exponent (0 = isotropic, 1 = Lambert).

### Hapke (Hensel)

$$\rho(\mu_0, \mu) = \frac{w}{\mu_0 + \mu} \left[ p(g) (1 + B_0 H(\mu_0) H(\mu)) + S(\mu_0, \mu) \right]$$

where:
- $w$ = single-scattering albedo
- $p(g)$ = phase function (Henyey-Greenstein or empirical)
- $B_0$ = opposition effect amplitude
- $H(\mu)$ = Hapke's auxiliary function
- $S(\mu_0, \mu)$ = shadow-hiding and coherent backscatter terms

### Lunar-Lambert

$$\rho(\mu_0, \mu) = L \mu_0 + (1-L) \frac{2}{\pi} \frac{\mu_0}{\mu_0 + \mu}$$

Hybrid model combining Lambert term (diffuse) and Minnaert-like term (forward scattering).

## Compilation

### Standalone Compilation

```bash
gcc -DP_PHOTOMODEL_STANDALONE -fopenmp -I. \
    -c p_photomodel.c -o p_photomodel.o
gcc -o test_photomodel test_photomodel.c p_photomodel.o -lm
```

### Integration with GRASS

Compiled as part of p.photomet, p.phocube, p.albedo modules via dependency tracking.

## Usage Example

Normalize an I/F measurement to standard geometry (i=30°, e=0°, g=30°) under Hapke model:

```c
double i_f = 0.15;
double params[3] = {0.25, 0.08, 0.1};  // w, b, c
double i_std = 30.0, e_std = 0.0, g_std = 30.0;

double rho_obs = p_photomodel_evaluate_hapkehen(i_f, 45.0, 30.0, 75.0, params);
double rho_std = p_photomodel_evaluate_hapkehen(i_f, i_std, e_std, g_std, params);
double rho_normalized = rho_std / rho_obs * i_f;
```

## Scientific References

- Hapke, B. W. (1981). "Bidirectional reflectance spectroscopy: 1. Theory." *Journal of Geophysical Research*, 86(B4), 3039–3054. https://doi.org/10.1029/JB086iB04p03039
- Hapke, B. W. (1984). "Bidirectional reflectance spectroscopy: 3. Correction for macroscopic roughness." *Icarus*, 59(1), 41–59. https://doi.org/10.1016/0019-1035(84)90054-X
- Hapke, B. W. (2002). "Bidirectional reflectance spectroscopy: 5. The coherent backscatter opposition effect and anisotropic scattering." *Journal of Geophysical Research*, 107(E5). https://doi.org/10.1029/2001JE001444
- Minnaert, M. (1941). "The Reciprocity Principle in Lunar Photometry." *Astrophysical Journal*, 93, 403–410. https://doi.org/10.1086/144279
- Henyey, L. G., & Greenstein, J. L. (1941). "Diffuse Radiation in the Galaxy." *Astrophysical Journal*, 93, 70–83. https://doi.org/10.1086/144246
- McEwen, A. S. (1991). "Photometric Functions for Photoclinometry and Other Applications." *Icarus*, 92(2), 298–311. https://doi.org/10.1016/0019-1035(91)90053-V

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
