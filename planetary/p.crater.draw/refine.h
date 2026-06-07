/*!
 * \file refine.h
 * \brief Sub-pixel centre and radius refinement for crater candidates.
 *
 * The coarse detector pass uses a stride of r/3 pixels, so its
 * candidate centres can be off by up to ~r/6 in each axis (and the
 * radius is quantised to the discrete scale-bank values). For
 * multi-ring basin aggregation this is too coarse: two concentric
 * detections of the same basin land at different (cx, cy) and never
 * group.
 *
 * refine_candidates_dem() takes a CandidateList after NMS and, for
 * each entry whose method is "dem" or "merged", performs a small
 * grid search over a (dx, dy, dr) cube centred on the original
 * (cx, cy, radius_m) that maximises the rim-vs-inner standardised
 * contrast score:
 *
 *   score = (rim_mean - inner_mean) / (rim_std + 0.5)
 *
 * The candidate's cx, cy, radius_m and confidence are mutated in
 * place. image-only candidates are left untouched (a future release
 * may add image-based refinement using the sun-shadow score).
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_DRAW_REFINE_H
#define P_CRATER_DRAW_REFINE_H

#include "p_crater_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Refine each DEM-derived candidate in place.
 *
 * \param cl        candidate list (mutated)
 * \param dem_name  GRASS raster name to load for refinement scoring
 * \param n_az      number of azimuth samples per rim circle
 *                  (16 matches the coarse detector default)
 * \return number of candidates that moved (centre or radius changed
 *         by more than 1 % of original radius)
 */
int refine_candidates_dem(CandidateList *cl,
                            const char *dem_name,
                            int n_az);

#ifdef __cplusplus
}
#endif

#endif /* P_CRATER_DRAW_REFINE_H */
