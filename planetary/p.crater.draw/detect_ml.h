/*!
 * \file detect_ml.h
 * \brief Phase-3 shallow-ML crater detector for p.crater.draw.
 *
 * P3 is implemented as a thin **stacking meta-detector** on top of
 * the classical P1 (DEM rim/floor) and P2 (image shadow-pair)
 * detectors. The per-candidate feature vector is:
 *
 *     f = [ conf_P1, conf_P2,
 *           rim_std_norm, inner_std_norm,
 *           bright_shadow_contrast, rim_interior_contrast ]
 *
 * (6 features). The final confidence is a weighted sum + bias passed
 * through a logistic squash:
 *
 *     conf_P3 = sigma( sum_i w_i * f_i + b )
 *
 * Weights and bias are loaded from a tiny binary model file
 * (`models/p_crater_draw_rf.bin`) carrying a magic header, version,
 * 6 weights and a bias as float32. If the model file is absent the
 * detector falls back to **default uniform weights** with all
 * features equally weighted - this is documented as a baseline
 * "stack the classical detectors uniformly" mode, not a learned ML.
 *
 * The intended training pipeline (scripts/train/p_crater_draw_rf.py,
 * NOT shipped in the .deb):
 *   1. Run p.crater.draw -method=both on a calibration scene.
 *   2. Take high-confidence merged detections (n_methods>=2,
 *      conf>0.85) as positives, random non-detection patches as
 *      negatives.
 *   3. Fit a logistic regression (or single-tree RF distilled to a
 *      linear function) over the 6 features.
 *   4. Export the 6 weights + bias to the .bin model file.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_DRAW_DETECT_ML_H
#define P_CRATER_DRAW_DETECT_ML_H

#include "p_crater_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P_CRATER_DRAW_ML_NFEATURES 6

/*! Loaded ML model parameters. */
typedef struct {
    float w[P_CRATER_DRAW_ML_NFEATURES];
    float bias;
    int   trained;   /*!< 1 if loaded from disk, 0 if uniform baseline */
} MLModel;

/*!
 * Load a model from \a path. Returns a zero-initialised baseline
 * if the path is NULL or the file cannot be opened, with `trained`
 * left at 0. Header format: magic 4B "PCDM" + version 4B int +
 * (4 + 6 + 1) float32s (weights + bias).
 */
MLModel ml_load_model(const char *path);

/*!
 * Run the shallow-ML rescoring pass. Reads from \a in candidates
 * already produced by detect_dem() and/or detect_image(), pairs
 * concentric candidates from the two methods to extract their
 * conf_P1 and conf_P2 channels, computes the 6-element feature
 * vector per merged candidate, applies the model, and writes a
 * new list to \a out. Standalone candidates with only one
 * detector get the missing channel filled with 0.
 *
 * The reweighted candidate carries method = "ml" (or "ml+merged")
 * and the new confidence in cand.confidence.
 *
 * \return number of ML candidates emitted.
 */
int detect_ml_rescore(const CandidateList *in,
                      const MLModel *model,
                      CandidateList *out);

#ifdef __cplusplus
}
#endif

#endif /* P_CRATER_DRAW_DETECT_ML_H */
