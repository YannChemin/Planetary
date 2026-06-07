/*!
 * \file p_spectra.h
 *
 * \brief Planetary library - spectral analysis for hyperspectral/multispectral data.
 *
 * Provides a pure-C spectral analysis library matching ISIS3's spectral
 * applications (spechighpass, speclowpass, specdivfilter, specpix) plus
 * standard planetary spectroscopy algorithms (band depth, continuum removal,
 * spectral angle mapper, band ratio).
 *
 * Core concepts
 * -------------
 *  PSpectraDef  — wavelength table mapping band index → (centre λ, FWHM width).
 *                 Ported from ISIS3 SpectralDefinition1D.  Loaded from a
 *                 two-column CSV (wavelength, width) or from caller arrays.
 *                 Supports multi-section definitions where wavelengths reverse
 *                 direction (as in some pushbroom detectors).
 *
 *  Spectrum     — a simple double[] of length nbands at one (sample, line).
 *                 All operations take the spectrum as a caller-owned array.
 *
 * Spectral operations
 * -------------------
 *  p_spectra_band_depth()       Absorption band depth relative to linear continuum.
 *  p_spectra_band_ratio()       Ratio of reflectance at two wavelengths.
 *  p_spectra_sam()              Spectral Angle Mapper angle [rad].
 *  p_spectra_continuum_remove() Remove linear continuum between two anchor λs.
 *  p_spectra_highpass()         Subtract spectral running mean (= spechighpass).
 *  p_spectra_divfilter()        Divide by spectral running mean (= specdivfilter).
 *
 * Row-level processing (OpenMP)
 * -----------------------------
 *  p_spectra_apply_row_*() functions operate over all samples in a raster row
 *  in parallel.  The input layout is:
 *    spectra[s * nbands + b]  = reflectance at sample s, band b  (row-major)
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 *         Algorithms: ISIS3 SpectralDefinition1D, spechighpass, specdivfilter
 *         (USGS Astrogeology, CC0-1.0); band-depth / SAM / continuum-removal
 *         follow USGS Spectroscopy Lab conventions.
 */

#ifndef GRASS_P_SPECTRA_H
#define GRASS_P_SPECTRA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Spectral definition                                                  */
/* ================================================================== */

/*! Opaque spectral definition handle. */
typedef struct PSpectraDef PSpectraDef;

/*!
 * \brief Create a spectral definition from caller-supplied arrays.
 *
 * \param nbands      number of spectral bands
 * \param wavelengths centre wavelengths, length nbands [any consistent unit]
 * \param widths      FWHM bandwidths, length nbands (NULL → all set to 0)
 * \return heap-allocated PSpectraDef*, or NULL on error
 */
PSpectraDef *p_spectra_def_create(int nbands,
                                   const double *wavelengths,
                                   const double *widths);

/*!
 * \brief Load a spectral definition from a two-column CSV file.
 *
 * Format: one row per band, columns = "wavelength,width".
 * Lines starting with '#' are skipped.  Handles multi-section definitions
 * (wavelengths that reverse direction) matching ISIS3 SpectralDefinition1D.
 *
 * \param csv_path   path to CSV file
 * \return heap-allocated PSpectraDef*, or NULL on parse error
 */
PSpectraDef *p_spectra_def_read_csv(const char *csv_path);

/*! \brief Number of bands in the spectral definition. */
int p_spectra_nbands(const PSpectraDef *sd);

/*! \brief Centre wavelength of band b (0-based). Returns NaN if out of range. */
double p_spectra_wavelength(const PSpectraDef *sd, int b);

/*! \brief FWHM width of band b (0-based). Returns NaN if out of range. */
double p_spectra_width(const PSpectraDef *sd, int b);

/*!
 * \brief Find the band whose centre wavelength is nearest to wl.
 *
 * \param sd  spectral definition
 * \param wl  target wavelength [same units as definition]
 * \param section  section index (0-based; use 0 if no multi-section)
 * \return 0-based band index of nearest match, or -1 if empty
 */
int p_spectra_find_band(const PSpectraDef *sd, double wl, int section);

/*! \brief Number of wavelength sections (direction reversals + 1). */
int p_spectra_section_count(const PSpectraDef *sd);

/*!
 * \brief Return the section index for band b (0-based).
 *
 * In a multi-section definition, band b may fall in section 0, 1, …
 * Matches ISIS3 SpectralDefinition1D::sectionNumber().
 */
int p_spectra_section_of_band(const PSpectraDef *sd, int b);

/*! \brief Free a PSpectraDef. */
void p_spectra_def_free(PSpectraDef *sd);

/* ================================================================== */
/* Single-spectrum operations                                           */
/*                                                                      */
/* All spectrum arrays are double[], length nbands, 0-indexed.          */
/* NaN values in the spectrum are treated as missing and skipped where  */
/* possible.                                                            */
/* ================================================================== */

/*!
 * \brief Linear-continuum band depth at wavelength wl_center.
 *
 * The continuum is a straight line between the reflectance at wl_left
 * and wl_right (interpolated from the nearest bands).
 *
 *   BD = 1 − R(wl_center) / Rc(wl_center)
 *
 * Returns NaN if any anchor band is missing or the continuum is ≤ 0.
 *
 * \param sd        spectral definition
 * \param spectrum  reflectance array [nbands]
 * \param wl_center centre wavelength of the absorption feature
 * \param wl_left   left anchor wavelength (continuum shoulder)
 * \param wl_right  right anchor wavelength (continuum shoulder)
 * \param section   wavelength section index (0 for single-section data)
 */
double p_spectra_band_depth(const PSpectraDef *sd,
                             const double *spectrum,
                             double wl_center,
                             double wl_left,
                             double wl_right,
                             int section);

/*!
 * \brief Reflectance ratio R(wl1) / R(wl2).
 *
 * Returns NaN if either band is missing or R(wl2) == 0.
 */
double p_spectra_band_ratio(const PSpectraDef *sd,
                             const double *spectrum,
                             double wl1, double wl2,
                             int section);

/*!
 * \brief Spectral Angle Mapper — angle between two spectra [0, π/2] radians.
 *
 *   SAM = arccos( dot(s1, s2) / (|s1| × |s2|) )
 *
 * NaN elements are skipped in dot product and norms.
 * Returns NaN if either spectrum is all-zero or all-NaN.
 *
 * \param nbands  length of both spectra
 * \param s1      test spectrum [nbands]
 * \param s2      reference spectrum [nbands]
 */
double p_spectra_sam(int nbands, const double *s1, const double *s2);

/*!
 * \brief Remove linear continuum between two anchor wavelengths.
 *
 * For each band b with wavelength wl_b ∈ [wl_left, wl_right]:
 *   output[b] = spectrum[b] / Rc(wl_b)
 * Bands outside the range are passed through unchanged (output[b] = spectrum[b]).
 * If Rc ≤ 0, output[b] is set to NaN.
 *
 * \param sd        spectral definition
 * \param spectrum  input reflectance [nbands]
 * \param nbands    length of spectrum
 * \param wl_left   left anchor wavelength
 * \param wl_right  right anchor wavelength
 * \param section   section index
 * \param output    caller-allocated output buffer [nbands]
 */
void p_spectra_continuum_remove(const PSpectraDef *sd,
                                 const double *spectrum,
                                 int nbands,
                                 double wl_left,
                                 double wl_right,
                                 int section,
                                 double *output);

/*!
 * \brief Spectral high-pass filter: output[b] = spectrum[b] − mean(window).
 *
 * Matches ISIS3 spechighpass.  The running mean is computed over a symmetric
 * window of width `window` bands centred on b, skipping NaN values.
 * If fewer than 2 valid neighbours exist, output[b] = NaN.
 *
 * \param spectrum  input [nbands]
 * \param nbands    number of bands
 * \param window    window width in bands (must be odd and ≥ 1)
 * \param output    caller-allocated output [nbands]
 */
void p_spectra_highpass(const double *spectrum,
                         int nbands,
                         int window,
                         double *output);

/*!
 * \brief Spectral divisive filter: output[b] = spectrum[b] / mean(window).
 *
 * Matches ISIS3 specdivfilter.  Same windowing as p_spectra_highpass.
 * Returns NaN where mean == 0.
 */
void p_spectra_divfilter(const double *spectrum,
                          int nbands,
                          int window,
                          double *output);

/* ================================================================== */
/* Row-level parallel operations (OpenMP)                              */
/*                                                                      */
/* spectra[s * nbands + b] = value at sample s, band b.               */
/* ================================================================== */

/*!
 * \brief Compute band depth for every sample in a raster row (OpenMP).
 *
 * \param sd        spectral definition
 * \param nsamples  number of samples (pixels)
 * \param nbands    number of spectral bands
 * \param spectra   input spectra [nsamples × nbands], row-major
 * \param wl_center centre wavelength of the absorption feature
 * \param wl_left   left continuum anchor wavelength
 * \param wl_right  right continuum anchor wavelength
 * \param section   section index
 * \param out       output band-depth per sample [nsamples] (caller-allocated)
 */
void p_spectra_apply_row_band_depth(const PSpectraDef *sd,
                                     int nsamples, int nbands,
                                     const double *spectra,
                                     double wl_center,
                                     double wl_left, double wl_right,
                                     int section,
                                     double *out);

/*!
 * \brief Compute band ratio for every sample in a raster row (OpenMP).
 */
void p_spectra_apply_row_band_ratio(const PSpectraDef *sd,
                                     int nsamples, int nbands,
                                     const double *spectra,
                                     double wl1, double wl2,
                                     int section,
                                     double *out);

/*!
 * \brief Compute SAM vs a reference spectrum for every sample (OpenMP).
 *
 * \param nsamples   number of samples
 * \param nbands     number of bands
 * \param spectra    test spectra [nsamples × nbands]
 * \param reference  single reference spectrum [nbands]
 * \param out        SAM angle [rad] per sample [nsamples]
 */
void p_spectra_apply_row_sam(int nsamples, int nbands,
                              const double *spectra,
                              const double *reference,
                              double *out);

/*!
 * \brief Apply spectral high-pass filter to every sample (OpenMP).
 *
 * \param nsamples  number of samples
 * \param nbands    number of bands
 * \param spectra   input [nsamples × nbands]
 * \param window    filter window width in bands
 * \param out       output [nsamples × nbands] (caller-allocated)
 */
void p_spectra_apply_row_highpass(int nsamples, int nbands,
                                   const double *spectra,
                                   int window,
                                   double *out);

/*!
 * \brief Apply spectral divisive filter to every sample (OpenMP).
 */
void p_spectra_apply_row_divfilter(int nsamples, int nbands,
                                    const double *spectra,
                                    int window,
                                    double *out);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_SPECTRA_H */
