/****************************************************************************
 * MODULE:       p.crater.draw (refine.c)
 * PURPOSE:      Sub-pixel centre and radius refinement for crater
 *               candidates. The coarse detector strides at r/3, so its
 *               centres are quantised; this pass runs a small (dx,dy,dr)
 *               grid search per DEM/merged candidate that maximises the
 *               standardised rim-vs-inner contrast, sharpening the centre
 *               so concentric rings of one basin land on a common point.
 *
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "p_crater_draw.h"
#include "refine.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Read the whole DEM into a contiguous double array (rows x cols).
 * NULL cells become NAN. Caller frees the returned buffer.            */
static double *load_dem(const char *name, int *nrows, int *ncols,
                        struct Cell_head *win)
{
    int fd = Rast_open_old(name, "");
    Rast_get_window(win);
    *nrows = win->rows;
    *ncols = win->cols;
    double *buf = G_malloc((size_t)(*nrows) * (*ncols) * sizeof(double));
    DCELL *row  = Rast_allocate_d_buf();
    for (int r = 0; r < *nrows; r++) {
        Rast_get_d_row(fd, row, r);
        for (int c = 0; c < *ncols; c++) {
            buf[(size_t)r * (*ncols) + c] =
                Rast_is_d_null_value(&row[c]) ? NAN : row[c];
        }
    }
    G_free(row);
    Rast_close(fd);
    return buf;
}

/* Bilinear lookup at fractional (row, col). Returns NAN on out-of-range
 * or if any of the 4 source cells is NULL. Identical to detect_dem.c.   */
static double dem_bilinear(const double *dem, int nr, int nc,
                           double r, double c)
{
    int r0 = (int)floor(r), c0 = (int)floor(c);
    int r1 = r0 + 1, c1 = c0 + 1;
    if (r0 < 0 || r1 >= nr || c0 < 0 || c1 >= nc) return NAN;
    double v00 = dem[(size_t)r0 * nc + c0];
    double v01 = dem[(size_t)r0 * nc + c1];
    double v10 = dem[(size_t)r1 * nc + c0];
    double v11 = dem[(size_t)r1 * nc + c1];
    if (isnan(v00) || isnan(v01) || isnan(v10) || isnan(v11)) return NAN;
    double dr = r - r0, dc = c - c0;
    return (1.0 - dr) * ((1.0 - dc) * v00 + dc * v01)
         +        dr  * ((1.0 - dc) * v10 + dc * v11);
}

/* Standardised rim-vs-inner contrast at a trial centre (row,col) and
 * radius r_pix. Mirrors detect_dem()'s scoring so refinement optimises
 * the very quantity the detector thresholds on. Returns -INFINITY when
 * the circle is too clipped by NULLs / region edge to score.            */
static double rim_score(const double *dem, int nr, int nc,
                        const double *cos_az, const double *sin_az,
                        int n_az, double row, double col, double r_pix)
{
    int n_ok_rim = 0, n_ok_in = 0;
    double rim_sum = 0.0, rim_sq = 0.0, in_sum = 0.0;
    for (int i = 0; i < n_az; i++) {
        double v = dem_bilinear(dem, nr, nc,
                                row + r_pix * sin_az[i],
                                col + r_pix * cos_az[i]);
        if (!isnan(v)) { rim_sum += v; rim_sq += v * v; n_ok_rim++; }
        v = dem_bilinear(dem, nr, nc,
                         row + 0.5 * r_pix * sin_az[i],
                         col + 0.5 * r_pix * cos_az[i]);
        if (!isnan(v)) { in_sum += v; n_ok_in++; }
    }
    if (n_ok_rim < n_az / 2 || n_ok_in < n_az / 4) return -INFINITY;

    double rim_mean = rim_sum / n_ok_rim;
    double in_mean  = in_sum  / n_ok_in;
    double rim_var  = rim_sq / n_ok_rim - rim_mean * rim_mean;
    if (rim_var < 0.0) rim_var = 0.0;
    double signal = rim_mean - in_mean;
    if (signal <= 0.0) return -INFINITY;
    return signal / (sqrt(rim_var) + 0.5);
}

int refine_candidates_dem(CandidateList *cl, const char *dem_name, int n_az)
{
    if (!cl || cl->n == 0) return 0;
    if (n_az <= 0) n_az = 16;

    struct Cell_head win;
    int nr = 0, nc = 0;
    double *dem = load_dem(dem_name, &nr, &nc, &win);
    if (!dem) return -1;

    double cell_m = 0.5 * (win.ew_res + win.ns_res);
    if (cell_m <= 0.0) { G_free(dem); return -1; }

    double *cos_az = G_malloc((size_t)n_az * sizeof(double));
    double *sin_az = G_malloc((size_t)n_az * sizeof(double));
    for (int i = 0; i < n_az; i++) {
        double a = 2.0 * M_PI * i / n_az;
        cos_az[i] = cos(a); sin_az[i] = sin(a);
    }

    /* Search half-spans: centre +/- r/3 (covers the detector stride),
     * radius +/- 15 %. Sampled on a fixed 7-point grid per axis -> the
     * step resolves to ~r/9 in centre and ~r/22 in radius, well below a
     * pixel for the basin-class radii this matters for.                 */
    const int   STEPS = 7;        /* odd -> includes the no-move centre */
    const double CTR_FRAC = 1.0 / 3.0;
    const double RAD_FRAC = 0.15;

    int n_moved = 0;
    for (int idx = 0; idx < cl->n; idx++) {
        CraterCandidate *cand = &cl->data[idx];
        if (strcmp(cand->method, "dem") != 0 &&
            strcmp(cand->method, "merged") != 0)
            continue;   /* image-only candidates left untouched */

        /* Map (cx,cy) to fractional pixel (row,col); radius to pixels. */
        double col0 = (cand->cx - win.west)  / win.ew_res - 0.5;
        double row0 = (win.north - cand->cy) / win.ns_res - 0.5;
        double r0_pix = cand->radius_m / cell_m;
        if (r0_pix < 2.0) continue;   /* too small to gain from refining */

        double ctr_span = r0_pix * CTR_FRAC;
        double rad_span = r0_pix * RAD_FRAC;
        double ctr_step = 2.0 * ctr_span / (STEPS - 1);
        double rad_step = 2.0 * rad_span / (STEPS - 1);

        double best_score = rim_score(dem, nr, nc, cos_az, sin_az, n_az,
                                      row0, col0, r0_pix);
        double best_row = row0, best_col = col0, best_r = r0_pix;

        for (int dr = 0; dr < STEPS; dr++) {
            double rr = r0_pix - rad_span + dr * rad_step;
            if (rr < 2.0) continue;
            for (int dy = 0; dy < STEPS; dy++) {
                double row = row0 - ctr_span + dy * ctr_step;
                for (int dx = 0; dx < STEPS; dx++) {
                    double col = col0 - ctr_span + dx * ctr_step;
                    double s = rim_score(dem, nr, nc, cos_az, sin_az,
                                         n_az, row, col, rr);
                    if (s > best_score) {
                        best_score = s; best_row = row;
                        best_col = col; best_r = rr;
                    }
                }
            }
        }

        if (!isfinite(best_score)) continue;   /* nothing scored */

        double new_cx = win.west  + (best_col + 0.5) * win.ew_res;
        double new_cy = win.north - (best_row + 0.5) * win.ns_res;
        double new_r  = best_r * cell_m;

        /* Did it move by > 1 % of the original radius (centre or radius)? */
        double dxm = new_cx - cand->cx, dym = new_cy - cand->cy;
        double moved = sqrt(dxm * dxm + dym * dym);
        double tol   = 0.01 * cand->radius_m;
        if (moved > tol || fabs(new_r - cand->radius_m) > tol)
            n_moved++;

        cand->cx        = new_cx;
        cand->cy        = new_cy;
        cand->radius_m  = new_r;
        cand->confidence = tanh(best_score / 3.0);
    }

    G_free(cos_az); G_free(sin_az); G_free(dem);
    G_message(_("Sub-pixel refinement: %d of %d candidate(s) adjusted."),
              n_moved, cl->n);
    return n_moved;
}
