# p_spectra Library

Spectral analysis functions for hyperspectral and multispectral planetary imagery.

## Overview

The p_spectra library provides C functions for common spectral analysis operations: band depth detection, spectral angle mapper (SAM), continuum removal, and filtering operations on planetary hyperspectral data (CRISM, OMEGA, TES).

## Supported Functions

| Function | Input | Output | Application |
|---|---|---|---|
| band_depth | Reflectance spectrum | Band depth (0–1) | Mineral detection, feature strength |
| continuum_remove | Reflectance spectrum | Normalized spectrum | Isolate absorption features |
| spectral_angle_mapper | Two spectra | Angle (radians) | Spectral matching, endmember identification |
| highpass_filter | Band image | Filtered image | Detail enhancement, texture |
| divfilter | Band image, divisor image | Normalized image | Band normalization, ratioing |

## API

### Band Depth

```c
double band_depth(const double *spectrum, int n_bands,
                  int center_band,
                  int left_band, int right_band);
```

Computes reflectance-based absorption depth:

$$D_B = 1 - \frac{R_{\text{center}}}{\text{continuum at center}}$$

where continuum is linearly interpolated between left and right reference wavelengths.

### Continuum Removal

```c
int continuum_remove(const double *spectrum,
                     const double *wavelengths,
                     int n_bands,
                     double *normalized_spectrum);
```

Removes the spectral baseline to emphasize absorption features:

$$R_{\text{norm}} = \frac{R(\lambda)}{R_{\text{continuum}}(\lambda)}$$

Computes convex hull from wavelength endpoints and local maxima; divides reflectance by hull.

### Spectral Angle Mapper (SAM)

```c
double spectral_angle_mapper(const double *spectrum1,
                             const double *spectrum2,
                             int n_bands);
```

Treats reflectance spectra as vectors; computes angle between them:

$$\theta = \arccos\left( \frac{\mathbf{R}_1 \cdot \mathbf{R}_2}{|\mathbf{R}_1| |\mathbf{R}_2|} \right)$$

Units: radians (convert to degrees with `theta * 180.0 / M_PI`).

**Interpretation:**
- $\theta \approx 0$ ← nearly identical spectra
- $\theta = 90°$ ← orthogonal spectra
- $\theta > 90°$ ← opposite trends

### High-Pass Filter

```c
int highpass_filter(const double *image, int rows, int cols,
                    int kernel_size,
                    double *filtered_image);
```

High-pass kernel (e.g., 5×5):

$$K = -\frac{1}{25} \begin{pmatrix} 1 & 1 & 1 & 1 & 1 \\ 1 & -24 & -24 & -24 & 1 \\ 1 & -24 & 200 & -24 & 1 \\ 1 & -24 & -24 & -24 & 1 \\ 1 & 1 & 1 & 1 & 1 \end{pmatrix}$$

Preserves high-frequency details; removes low-frequency (illumination) variation.

### Division Filter

```c
int divfilter(const double *numerator, const double *denominator,
              int rows, int cols,
              double min_denominator,
              double *ratio_image);
```

Band ratio with division-by-zero protection:

$$\text{ratio}[i] = \begin{cases}
\frac{\text{num}[i]}{\text{denom}[i]} & \text{if } \text{denom}[i] > \text{min\_denom} \\
0 & \text{otherwise}
\end{cases}$$

## Compilation

### Standalone Compilation

```bash
gcc -DP_SPECTRA_STANDALONE -fopenmp -I. \
    -c p_spectra.c -o p_spectra.o
gcc -o test_spectra test_spectra.c p_spectra.o -lm
```

### Integration with GRASS

Compiled as part of p.spectral.planet and p.mineral.indices modules.

## Usage Example

Compute OLINDEX (olivine index) on CRISM data:

```c
/* CRISM band wavelengths (approximate, in µm) */
double crism_wl[] = {0.362, 0.386, 0.413, ..., 3.980};  /* 544 bands */

/* Reflectance at 3 pixels */
double olindex[3];

for (int pix = 0; pix < 3; pix++) {
    double spectrum[] = {R_0362, R_0386, ..., R_3980};  /* read from file */
    
    /* OLINDEX: depth of 1.0 µm olivine absorption band */
    olindex[pix] = band_depth(spectrum, 544, band_center_1um,
                               band_left, band_right);
}

printf("OLINDEX: %.3f, %.3f, %.3f\n", olindex[0], olindex[1], olindex[2]);
```

## Scientific References

- Clark, R. N., & Roush, T. L. (1984). "Reflectance Spectroscopy: Quantitative Analysis Techniques for Remote Sensing Applications." *Journal of Geophysical Research*, 89(B7), 6329–6340. https://doi.org/10.1029/JB089iB07p06329
- Kruse, F. A., Lefkoff, A. B., Boardman, J. W., et al. (1993). "The Spectral Image Processing System (SIPS)—Interactive Visualization and Analysis of Imaging Spectrometer Data." *Remote Sensing of Environment*, 44(2–3), 145–163. https://doi.org/10.1016/0034-4257(93)90013-N
- Pelkey, S. M., Mustard, J. F., Murchie, S., et al. (2007). "CRISM Multispectral Summed Data Product Calibration and Limitations." *Journal of Geophysical Research*, 112(E8), E08S14. https://doi.org/10.1029/2006JE002831
- Viviano-Beck, C. E., Seelos, F. P., Murchie, S. L., et al. (2014). "Revised CRISM Spectral Parameters Using Inflight Radiometric Recalibration and Eq-Lc Spectral Unmixing." *Journal of Geophysical Research: Planets*, 119(6), 1403–1431. https://doi.org/10.1002/2014JE004627

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
