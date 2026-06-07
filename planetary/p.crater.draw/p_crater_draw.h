/*!
 * \file p_crater_draw.h
 * \brief Shared types and helpers for p.crater.draw detectors.
 *
 * The module supports two detector strategies in v1 (P1+P2):
 *   - DEM-based depression + rim radial-profile detector (Strategy A)
 *   - Image-based sun-shadow paired template detector (Strategy B)
 *
 * Each detector emits a list of CraterCandidate structs; a common NMS
 * + polygon-write stage merges them into the final GRASS vector
 * output. The intermediate format is plain C arrays so detectors stay
 * decoupled from GRASS internals and can be reused.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_DRAW_H
#define P_CRATER_DRAW_H

#include <grass/gis.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! One detected crater candidate (output of any detector). */
typedef struct {
    double cx, cy;        /*!< centre in map (projected) coordinates */
    double radius_m;      /*!< radius [m]                            */
    double confidence;    /*!< detector score, normalised 0..1       */
    char   method[16];    /*!< "dem", "image", or "merged"           */
    int    n_methods;     /*!< 1 for single-detector, 2+ if merged   */
    double dD_simple;     /*!< simple-crater d/D baked at detection;
                              0 = "not set, downstream uses default" */
    int    basin_id;      /*!< 0 = standalone; >0 = multi-ring group */
    int    ring_index;    /*!< 0 = standalone; 1=innermost ring, 2.. */
} CraterCandidate;

/*! Growable candidate list. */
typedef struct {
    CraterCandidate *data;
    int n, cap;
} CandidateList;

void cl_init(CandidateList *cl);
void cl_push(CandidateList *cl, const CraterCandidate *c);
void cl_free(CandidateList *cl);

/*!
 * Configuration shared by all detectors. Diameters in metres.
 * The 10 km cap is a deliberate v1 ceiling (see manual FUTURE WORK).
 */
typedef struct {
    double  d_min;        /*!< minimum diameter [m]              */
    double  d_max;        /*!< maximum diameter [m] (default 10 km) */
    double  threshold;    /*!< detection threshold (0..1)         */
    int     n_scales;     /*!< how many log-spaced radii to scan  */
    int     n_az_samples; /*!< azimuthal samples around the rim   */
    int     n_threads;    /*!< OpenMP threads (0 = library default) */
    int     use_opencl;   /*!< 1 = try OpenCL when available      */
    const char *body;     /*!< body name for the manual log line  */
} DrawConfig;

/* ---- Strategy A: DEM-based detector ----------------------------- */

/*!
 * Detect craters from a DEM raster. Opens the raster in read-only,
 * scans the whole region, emits candidates into \a out.
 *
 * Algorithm summary:
 *   1. Read the entire DEM into RAM (current GRASS region only).
 *   2. For each log-spaced scale r (pixels):
 *        For each candidate centre (cx, cy):
 *          Sample N_AZ DEM values on the rim circle of radius r and
 *          on an inner circle of radius r/2.
 *          Score = (rim_mean - inner_mean) / (rim_std + epsilon)
 *        Threshold and emit candidates with score >= cfg->threshold.
 *   3. Parallelised across candidate centres via OpenMP.
 *
 * \return 0 on success, -1 on fatal error.
 */
int detect_dem(const char *dem_name,
               const DrawConfig *cfg,
               CandidateList *out);

/* ---- Strategy B: Image sun-shadow detector ---------------------- */

/*!
 * Detect craters from a panchromatic (single-band) image raster
 * using the sun-shadow geometry.
 *
 * \param sun_azimuth_deg  Azimuth of the sun (0=N, clockwise) at the
 *                         time the image was taken. Required.
 */
int detect_image(const char *image_name,
                  double sun_azimuth_deg,
                  const DrawConfig *cfg,
                  CandidateList *out);

/* ---- NMS + vector output --------------------------------------- */

/*!
 * Non-maximum suppression: keep only the highest-confidence candidate
 * within each cluster of IoU >= \a iou_threshold. Modifies the list
 * in place. Optionally tags clusters spanning multiple methods.
 */
void apply_nms(CandidateList *cl, double iou_threshold);

/*!
 * \brief Multi-ring basin aggregation.
 *
 * Groups candidates whose centres agree within \a centre_tol_frac of
 * the smaller radius AND whose radii differ by at least
 * \a min_radius_ratio (so they are distinct rings, not just NMS-
 * surviving near-duplicates). Tagged groups receive a positive
 * basin_id (sequentially assigned, starting at 1) and ring_index
 * (1 = innermost / smallest radius).
 *
 * Sets standalone (un-grouped) candidates to basin_id = 0,
 * ring_index = 0. Returns the number of basins detected.
 */
int aggregate_multiring(CandidateList *cl,
                         double centre_tol_frac,
                         double min_radius_ratio);

/*!
 * Write the candidate list as a GRASS polygon vector with the
 * attribute schema p.crater and p.crater.freq expect:
 *   cat, cx, cy, D_eq, axis_ratio, azimuth_deg, confidence,
 *   method, n_methods
 *
 * Polygons are 48-vertex circles (axis_ratio = 1.0 in v1).
 */
int write_candidates_vector(const char *out_name,
                             const CandidateList *cl);

#ifdef __cplusplus
}
#endif

#endif /* P_CRATER_DRAW_H */
