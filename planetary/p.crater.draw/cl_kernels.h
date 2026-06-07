/*!
 * \file cl_kernels.h
 * \brief OpenCL kernel sources for the DEM radial-profile and image
 *        sun-shadow detectors, embedded as C string literals.
 *
 * Each kernel processes ONE (row, col, scale) candidate per work item.
 * Inputs are the raster (read-only buffer of doubles, NULL = NaN
 * encoded as 0x7FF8000000000000 = NAN bit pattern), the trig sample
 * tables, and the geometry / threshold parameters. Output is a flat
 * confidence array sized rows*cols; downstream CPU code thresholds
 * and emits CraterCandidate records.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_DRAW_CL_KERNELS_H
#define P_CRATER_DRAW_CL_KERNELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The DEM radial-profile detector kernel - same scoring as
 * detect_dem.c's per-candidate inner loop. */
extern const char *p_crater_draw_cl_dem_src;

/* The image sun-shadow paired-arc detector kernel - same scoring as
 * detect_image.c's per-candidate inner loop. */
extern const char *p_crater_draw_cl_image_src;

#ifdef __cplusplus
}
#endif

#endif
