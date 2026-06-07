/****************************************************************************
 * MODULE:       p.crater.draw (detect_image.c)
 * PURPOSE:      Strategy B - image-based crater detection from the
 *               sun-azimuth-dependent shadow/highlight pair on the rim.
 *
 *               Algorithm: for each scale r and each candidate centre,
 *               sample the image on two arcs of the rim circle:
 *                 - "bright arc" centred on the sun-azimuth direction
 *                 - "shadow arc" centred on the anti-sun direction
 *               plus an inner-disk control sample. The score is
 *                 ((bright_mean - shadow_mean) / local_std)
 *               normalised through tanh into 0..1.
 *
 *               This catches fresh circular craters where the rim
 *               casts a half-moon shadow opposite the sun.
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

#ifdef _OPENMP
#include <omp.h>
#endif

#include "p_crater_draw.h"
#include "opencl_runtime.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Identical pattern to detect_dem load_dem - kept local to avoid a
 * cross-module dependency on a shared loader.                          */
static double *load_image(const char *name, int *nrows, int *ncols,
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

static double bilinear(const double *img, int nr, int nc,
                        double r, double c)
{
    int r0 = (int)floor(r), c0 = (int)floor(c);
    int r1 = r0 + 1, c1 = c0 + 1;
    if (r0 < 0 || r1 >= nr || c0 < 0 || c1 >= nc) return NAN;
    double v00 = img[(size_t)r0 * nc + c0];
    double v01 = img[(size_t)r0 * nc + c1];
    double v10 = img[(size_t)r1 * nc + c0];
    double v11 = img[(size_t)r1 * nc + c1];
    if (isnan(v00) || isnan(v01) || isnan(v10) || isnan(v11)) return NAN;
    double dr = r - r0, dc = c - c0;
    return (1.0 - dr) * ((1.0 - dc) * v00 + dc * v01)
         +        dr  * ((1.0 - dc) * v10 + dc * v11);
}

/* ----------------------------------------------------------------- */
int detect_image(const char *image_name,
                  double sun_azimuth_deg,
                  const DrawConfig *cfg,
                  CandidateList *out)
{
    struct Cell_head win;
    int nr = 0, nc = 0;
    double *img = load_image(image_name, &nr, &nc, &win);
    if (!img) return -1;

    double cell_m = 0.5 * (win.ew_res + win.ns_res);
    if (cell_m <= 0.0) { G_free(img); return -1; }

    /* Convert compass azimuth (0=N, CW) to math (0=E, CCW). */
    double sun_az_math = (90.0 - sun_azimuth_deg) * M_PI / 180.0;

    double r_min_pix = cfg->d_min * 0.5 / cell_m;
    double r_max_pix = cfg->d_max * 0.5 / cell_m;
    if (r_max_pix < r_min_pix + 1.0) r_max_pix = r_min_pix + 1.0;
    if (r_min_pix < 2.0) r_min_pix = 2.0;
    if (r_max_pix > 0.5 * (nr < nc ? nr : nc) - 2.0)
        r_max_pix = 0.5 * (nr < nc ? nr : nc) - 2.0;

    int K = cfg->n_scales > 0 ? cfg->n_scales : 8;
    double *radii = G_malloc((size_t)K * sizeof(double));
    double lo = log(r_min_pix), hi = log(r_max_pix);
    for (int k = 0; k < K; k++)
        radii[k] = exp(lo + (hi - lo) * k / (double)(K - 1 > 0 ? K - 1 : 1));

    /* Pre-compute the rim arc samples.  We pick two opposing arcs of
     * +/- pi/3 centred on the sun and anti-sun directions, plus 4
     * inner control points at radius 0.4 r.                          */
    int N_ARC = 9;  /* 9 samples per arc -> 18 rim samples total       */
    double *cos_bright = G_malloc((size_t)N_ARC * sizeof(double));
    double *sin_bright = G_malloc((size_t)N_ARC * sizeof(double));
    double *cos_dark   = G_malloc((size_t)N_ARC * sizeof(double));
    double *sin_dark   = G_malloc((size_t)N_ARC * sizeof(double));
    for (int i = 0; i < N_ARC; i++) {
        double dt = (i - (N_ARC - 1) * 0.5) * (M_PI / 3.0) / (N_ARC - 1);
        double ab = sun_az_math + dt;
        double ad = sun_az_math + M_PI + dt;
        cos_bright[i] = cos(ab); sin_bright[i] = sin(ab);
        cos_dark  [i] = cos(ad); sin_dark  [i] = sin(ad);
    }
    double cos_inner[4], sin_inner[4];
    for (int i = 0; i < 4; i++) {
        double a = i * (M_PI / 2.0);
        cos_inner[i] = cos(a); sin_inner[i] = sin(a);
    }

#ifdef _OPENMP
    if (cfg->n_threads > 0) omp_set_num_threads(cfg->n_threads);
    int n_tot_threads = omp_get_max_threads();
#else
    int n_tot_threads = 1;
#endif

    CandidateList *per_thread = G_calloc((size_t)n_tot_threads,
                                           sizeof(CandidateList));
    for (int t = 0; t < n_tot_threads; t++) cl_init(&per_thread[t]);

    G_message(_("Image detector: %d scales, sun azimuth = %.1f deg, "
                 "region %dx%d, radius search [%.1f, %.1f] pixels"),
              K, sun_azimuth_deg, nr, nc, r_min_pix, r_max_pix);

    int gpu = cfg->use_opencl && p_crater_draw_opencl_available(0);

    for (int k = 0; k < K; k++) {
        double r = radii[k];
        int    ir = (int)ceil(r) + 1;
        int    stride = (int)fmax(1.0, r / 3.0);

        /* ---------- GPU fast path ---------- */
        if (gpu) {
            double *conf = p_crater_draw_cl_run_image(
                img, nr, nc, r, N_ARC,
                cos_bright, sin_bright, cos_dark, sin_dark,
                cos_inner,  sin_inner,  cfg->threshold);
            if (conf) {
                CandidateList *tl = &per_thread[0];
                for (int row = ir; row < nr - ir; row += stride) {
                    for (int col = ir; col < nc - ir; col += stride) {
                        double c = conf[(size_t)row * nc + col];
                        if (c <= 0.0) continue;
                        CraterCandidate cand;
                        cand.cx         = win.west  + (col + 0.5) * win.ew_res;
                        cand.cy         = win.north - (row + 0.5) * win.ns_res;
                        cand.radius_m   = r * cell_m;
                        cand.confidence = c;
                        strncpy(cand.method, "image", sizeof(cand.method));
                        cand.n_methods  = 1;
                        cand.dD_simple  = 0.0;
                        cand.basin_id   = 0;
                        cand.ring_index = 0;
                        cl_push(tl, &cand);
                    }
                }
                G_free(conf);
                G_message(_("  scale %d/%d  r=%.0f m   (GPU)"),
                          k + 1, K, 2.0 * r * cell_m);
                continue;
            }
            G_warning(_("OpenCL image kernel failed at scale %d - "
                         "using OpenMP for the remainder"), k);
            gpu = 0;
        }

#pragma omp parallel for schedule(dynamic, 4)
        for (int row = ir; row < nr - ir; row += stride) {
#ifdef _OPENMP
            int tid = omp_get_thread_num();
#else
            int tid = 0;
#endif
            CandidateList *tl = &per_thread[tid];
            for (int col = ir; col < nc - ir; col += stride) {
                double bsum = 0.0, dsum = 0.0, isum = 0.0;
                int nb = 0, nd = 0, ni = 0;
                double allsum = 0.0, allsq = 0.0;
                int nall = 0;

                for (int i = 0; i < N_ARC; i++) {
                    double v;
                    v = bilinear(img, nr, nc,
                                  row + r * sin_bright[i],
                                  col + r * cos_bright[i]);
                    if (!isnan(v)) { bsum += v; nb++;
                        allsum += v; allsq += v * v; nall++; }
                    v = bilinear(img, nr, nc,
                                  row + r * sin_dark[i],
                                  col + r * cos_dark[i]);
                    if (!isnan(v)) { dsum += v; nd++;
                        allsum += v; allsq += v * v; nall++; }
                }
                if (nb < N_ARC / 2 || nd < N_ARC / 2) continue;

                for (int i = 0; i < 4; i++) {
                    double v = bilinear(img, nr, nc,
                                          row + 0.4 * r * sin_inner[i],
                                          col + 0.4 * r * cos_inner[i]);
                    if (!isnan(v)) { isum += v; ni++; }
                }
                if (ni < 2) continue;

                double bmean = bsum / nb;
                double dmean = dsum / nd;
                double imean = isum / ni;
                double allmean = allsum / nall;
                double allvar = allsq / nall - allmean * allmean;
                if (allvar < 0.0) allvar = 0.0;
                double allstd = sqrt(allvar);

                /* Two-channel score: sun-side bright relative to
                 * shadow-side dark, and rim contrast vs interior.  */
                double s1 = (bmean - dmean) / (allstd + 1e-9);
                double s2 = (0.5 * (bmean + dmean) - imean) / (allstd + 1e-9);
                /* Real crater wants s1 large positive AND s2 modest
                 * positive (rim brighter than inside on average).   */
                double score = s1 + 0.5 * fmax(0.0, s2);
                if (score <= 0.0) continue;
                double conf  = tanh(score / 3.0);
                if (conf < cfg->threshold) continue;

                CraterCandidate cand;
                cand.cx         = win.west  + (col + 0.5) * win.ew_res;
                cand.cy         = win.north - (row + 0.5) * win.ns_res;
                cand.radius_m   = r * cell_m;
                cand.confidence = conf;
                strncpy(cand.method, "image", sizeof(cand.method));
                cand.n_methods  = 1;
                cand.dD_simple  = 0.0;
                cand.basin_id   = 0;
                cand.ring_index = 0;
                cl_push(tl, &cand);
            }
        }
        G_message(_("  scale %d/%d  r=%.0f m   accumulated candidates: %d"),
                  k + 1, K, 2.0 * r * cell_m,
                  ({
                      int s = 0;
                      for (int t = 0; t < n_tot_threads; t++) s += per_thread[t].n;
                      s;
                  }));
    }

    for (int t = 0; t < n_tot_threads; t++) {
        for (int i = 0; i < per_thread[t].n; i++)
            cl_push(out, &per_thread[t].data[i]);
        cl_free(&per_thread[t]);
    }
    G_free(per_thread);
    G_free(radii); G_free(cos_bright); G_free(sin_bright);
    G_free(cos_dark); G_free(sin_dark); G_free(img);

    G_message(_("Image detector emitted %d raw candidates "
                 "(before NMS)."), out->n);
    return 0;
}
