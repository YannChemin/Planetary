/****************************************************************************
 * MODULE:       p.crater.draw (detect_dem.c)
 * PURPOSE:      Strategy A - DEM-based crater detection via multi-scale
 *               rim/floor radial-profile analysis. Per-candidate score is
 *               the standardised rim-elevation contrast against an
 *               inner-floor sample population. OpenMP-parallelised over
 *               candidate centres at each scale.
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

static inline double dem_at(const double *dem, int nr, int nc, int r, int c)
{
    if (r < 0 || r >= nr || c < 0 || c >= nc) return NAN;
    return dem[(size_t)r * nc + c];
}

/* Bilinear lookup at fractional (row, col). Returns NAN on out-of-range
 * or if any of the 4 source cells is NULL.                              */
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

/* ----------------------------------------------------------------- */
int detect_dem(const char *dem_name,
                const DrawConfig *cfg,
                CandidateList *out)
{
    struct Cell_head win;
    int nr = 0, nc = 0;
    double *dem = load_dem(dem_name, &nr, &nc, &win);
    if (!dem) return -1;

    /* Mean cell size in metres - the GRASS region is assumed to be in
     * a projected CRS with metric units. For lat/lon regions the user
     * must first project via v.proj / r.proj.                          */
    double cell_m = 0.5 * (win.ew_res + win.ns_res);
    if (cell_m <= 0.0) { G_free(dem); return -1; }

    double r_min_pix = cfg->d_min * 0.5 / cell_m;
    double r_max_pix = cfg->d_max * 0.5 / cell_m;
    if (r_max_pix < r_min_pix + 1.0) r_max_pix = r_min_pix + 1.0;
    if (r_min_pix < 2.0) r_min_pix = 2.0;
    if (r_max_pix > 0.5 * (nr < nc ? nr : nc) - 2.0)
        r_max_pix = 0.5 * (nr < nc ? nr : nc) - 2.0;

    /* Pre-compute log-spaced radii. */
    int K = cfg->n_scales > 0 ? cfg->n_scales : 8;
    double *radii = G_malloc((size_t)K * sizeof(double));
    double lo = log(r_min_pix), hi = log(r_max_pix);
    for (int k = 0; k < K; k++)
        radii[k] = exp(lo + (hi - lo) * k / (double)(K - 1 > 0 ? K - 1 : 1));

    int N_AZ = cfg->n_az_samples > 0 ? cfg->n_az_samples : 16;
    double *cos_az = G_malloc((size_t)N_AZ * sizeof(double));
    double *sin_az = G_malloc((size_t)N_AZ * sizeof(double));
    for (int i = 0; i < N_AZ; i++) {
        double a = 2.0 * M_PI * i / N_AZ;
        cos_az[i] = cos(a); sin_az[i] = sin(a);
    }

    /* Each thread accumulates into its own private list, then we
     * merge once at the end - avoids OpenMP critical sections inside
     * the hot loop.                                                  */
#ifdef _OPENMP
    if (cfg->n_threads > 0) omp_set_num_threads(cfg->n_threads);
    int n_tot_threads = omp_get_max_threads();
#else
    int n_tot_threads = 1;
#endif

    CandidateList *per_thread = G_calloc((size_t)n_tot_threads,
                                           sizeof(CandidateList));
    for (int t = 0; t < n_tot_threads; t++) cl_init(&per_thread[t]);

    G_message(_("DEM detector: %d scales x %d threads, region %dx%d cells, "
                 "radius search [%.1f, %.1f] pixels"),
              K, n_tot_threads, nr, nc, r_min_pix, r_max_pix);

    /* GPU available? Probe once; fall back to OpenMP if not. */
    int gpu = cfg->use_opencl && p_crater_draw_opencl_available(0);

    for (int k = 0; k < K; k++) {
        double r = radii[k];
        int    ir = (int)ceil(r) + 1;
        int    rows_done = 0;
        /* Stride: don't test every pixel - centres closer than r/3
         * cannot both survive NMS anyway, so subsample.                */
        int stride = (int)fmax(1.0, r / 3.0);

        /* ---------- GPU fast path ---------- */
        if (gpu) {
            double *conf = p_crater_draw_cl_run_dem(
                dem, nr, nc, r, N_AZ, cos_az, sin_az, cfg->threshold);
            if (conf) {
                /* Subsample by `stride` to emulate the CPU loop's
                 * candidate placement, push to thread 0's list. */
                CandidateList *tl = &per_thread[0];
                for (int row = ir; row < nr - ir; row += stride) {
                    for (int col = ir; col < nc - ir; col += stride) {
                        double c = conf[(size_t)row * nc + col];
                        if (c <= 0.0) continue;
                        CraterCandidate cand;
                        cand.cx        = win.west  + (col + 0.5) * win.ew_res;
                        cand.cy        = win.north - (row + 0.5) * win.ns_res;
                        cand.radius_m  = r * cell_m;
                        cand.confidence = c;
                        strncpy(cand.method, "dem", sizeof(cand.method));
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
                continue;   /* skip the OpenMP CPU path for this scale */
            }
            /* GPU dispatch failed; quietly fall back to CPU for this k. */
            G_warning(_("OpenCL DEM kernel failed at scale %d - "
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
                /* Sample DEM along outer rim and inner half-radius. */
                double rim[64], inner[64];
                int n_ok_rim = 0, n_ok_in = 0;
                double rim_sum = 0.0, rim_sq = 0.0, in_sum = 0.0;
                for (int i = 0; i < N_AZ; i++) {
                    double rr = row + r * sin_az[i];
                    double cc = col + r * cos_az[i];
                    double v = dem_bilinear(dem, nr, nc, rr, cc);
                    if (!isnan(v)) {
                        rim[n_ok_rim++] = v;
                        rim_sum += v;
                        rim_sq  += v * v;
                    }
                    rr = row + 0.5 * r * sin_az[i];
                    cc = col + 0.5 * r * cos_az[i];
                    v = dem_bilinear(dem, nr, nc, rr, cc);
                    if (!isnan(v)) {
                        inner[n_ok_in++] = v;
                        in_sum += v;
                    }
                }
                if (n_ok_rim < N_AZ / 2 || n_ok_in < N_AZ / 4) continue;

                double rim_mean = rim_sum / n_ok_rim;
                double in_mean  = in_sum  / n_ok_in;
                double rim_var  = rim_sq / n_ok_rim - rim_mean * rim_mean;
                if (rim_var < 0.0) rim_var = 0.0;
                double rim_std  = sqrt(rim_var);

                /* Crater: rim higher than floor by a comfortable
                 * margin in units of the rim-elevation std-dev.       */
                double signal  = rim_mean - in_mean;
                if (signal <= 0.0) continue;   /* no rim, no crater    */
                double score   = signal / (rim_std + 0.5);
                /* Map to (0, 1): tanh on positive scores only. This
                 * way "no signal" -> 0, not 0.5.                       */
                double conf    = tanh(score / 3.0);

                if (conf < cfg->threshold) continue;

                /* Map (row, col, r_pixels) back to (x, y, r_m). */
                CraterCandidate cand;
                cand.cx        = win.west  + (col + 0.5) * win.ew_res;
                cand.cy        = win.north - (row + 0.5) * win.ns_res;
                cand.radius_m  = r * cell_m;
                cand.confidence = conf;
                strncpy(cand.method, "dem", sizeof(cand.method));
                cand.n_methods = 1;
                cand.dD_simple = 0.0;
                cand.basin_id  = 0;
                cand.ring_index = 0;
                cl_push(tl, &cand);
            }
#pragma omp atomic
            rows_done += stride;
        }
        G_message(_("  scale %d/%d  r=%.0f m   accumulated candidates: %d"),
                  k + 1, K, 2.0 * r * cell_m,
                  ({
                      int s = 0;
                      for (int t = 0; t < n_tot_threads; t++) s += per_thread[t].n;
                      s;
                  }));
    }

    /* Merge per-thread lists. */
    for (int t = 0; t < n_tot_threads; t++) {
        for (int i = 0; i < per_thread[t].n; i++)
            cl_push(out, &per_thread[t].data[i]);
        cl_free(&per_thread[t]);
    }
    G_free(per_thread);

    G_free(radii); G_free(cos_az); G_free(sin_az); G_free(dem);
    G_message(_("DEM detector emitted %d raw candidates "
                 "(before NMS)."), out->n);
    return 0;
}
