/*!
 * \file opencl_runtime.h
 * \brief Optional OpenCL acceleration layer for p.crater.draw.
 *
 * The module always ships with an OpenMP CPU path. When built with
 * -DHAVE_OPENCL and run with the -c flag, this runtime probes the
 * available OpenCL platforms and devices, picks the most-capable
 * GPU (preferring discrete > integrated > CPU class), and exposes
 * helper functions for the detector inner loops to dispatch their
 * convolution / radial-profile kernels.
 *
 * In v1 (where HAVE_OPENCL is not defined), only the diagnostic
 * functions are compiled - they return "not available" and the
 * caller transparently falls back to OpenMP. This file is the
 * single place to add the real CL kernels in a future release;
 * the detector files do not need to change.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_DRAW_OPENCL_RUNTIME_H
#define P_CRATER_DRAW_OPENCL_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Probe OpenCL devices on the host.
 * \param verbose_log if non-zero, prints platform/device info via G_message.
 * \return 1 if at least one OpenCL device is usable; 0 otherwise.
 */
int p_crater_draw_opencl_available(int verbose_log);

/*!
 * \brief Short human-readable description of the OpenCL backend
 * (e.g. "NVIDIA RTX 4090 via OpenCL 3.0", or "no OpenCL support
 * compiled in" when HAVE_OPENCL is undefined).
 */
const char *p_crater_draw_opencl_describe(void);

/*!
 * \brief Run the DEM detector kernel on the GPU for one scale.
 *
 * Returns a freshly G_malloc'd nrows*ncols double buffer of confidences
 * (0 = below threshold; > 0 = candidate), or NULL on any OpenCL error
 * (caller must fall back to the OpenMP path).
 */
double *p_crater_draw_cl_run_dem(const double *dem,
                                   int nrows, int ncols,
                                   double radius_px,
                                   int n_az,
                                   const double *cos_az,
                                   const double *sin_az,
                                   double threshold);

/*!
 * \brief Run the image detector kernel on the GPU for one scale.
 */
double *p_crater_draw_cl_run_image(const double *img,
                                     int nrows, int ncols,
                                     double radius_px,
                                     int n_arc,
                                     const double *cos_b,
                                     const double *sin_b,
                                     const double *cos_d,
                                     const double *sin_d,
                                     const double *cos_in,
                                     const double *sin_in,
                                     double threshold);

/*! Release cached OpenCL objects (program/context). Idempotent. */
void p_crater_draw_opencl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* P_CRATER_DRAW_OPENCL_RUNTIME_H */
