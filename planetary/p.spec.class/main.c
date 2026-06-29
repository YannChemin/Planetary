/****************************************************************************
 *
 * MODULE:       p.spec.class
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Spectral classification of multi-band planetary rasters.
 *               Two modes:
 *               - kmeans:  unsupervised k-means clustering (random seeding
 *                          with k-means++ initialisation to avoid degenerate
 *                          clusters).
 *               - sam:     supervised Spectral Angle Mapper classification
 *                          against a reference spectrum CSV; pixels with SAM
 *                          angle ≤ threshold are assigned to the class.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE file.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include "../../libs/p_spectra/p_spectra.h"

/* ── SAM angle between two spectra (0-based, length nbands) ────────── */
static double sam_angle(int nbands, const double *a, const double *b)
{
    double dot = 0.0, na2 = 0.0, nb2 = 0.0;
    for (int i = 0; i < nbands; i++) {
        if (a[i] != a[i] || b[i] != b[i]) continue; /* skip NaN */
        dot += a[i] * b[i];
        na2 += a[i] * a[i];
        nb2 += b[i] * b[i];
    }
    if (na2 <= 0.0 || nb2 <= 0.0) return M_PI / 2.0;
    double cosval = dot / (sqrt(na2) * sqrt(nb2));
    if (cosval >  1.0) cosval =  1.0;
    if (cosval < -1.0) cosval = -1.0;
    return acos(cosval);
}

/* ── k-means++ seeding: choose k centres with D² probability ────────── */
static void kmeans_pp_init(int n, int nbands, const double *pixels,
                            int k, double *centres, unsigned int *seed)
{
    /* First centre: random pixel */
    int first = rand_r(seed) % n;
    memcpy(centres, pixels + (size_t)first * nbands, (size_t)nbands * sizeof(double));

    double *dist2 = (double *)G_malloc((size_t)n * sizeof(double));

    for (int c = 1; c < k; c++) {
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            double best = 1e300;
            for (int cc = 0; cc < c; cc++) {
                double d2 = 0.0;
                for (int b = 0; b < nbands; b++) {
                    double diff = pixels[(size_t)i * nbands + b]
                                  - centres[(size_t)cc * nbands + b];
                    d2 += diff * diff;
                }
                if (d2 < best) best = d2;
            }
            dist2[i] = best;
            sum += best;
        }
        /* Weighted random draw */
        double r = ((double)rand_r(seed) / RAND_MAX) * sum;
        double acc = 0.0;
        int chosen = 0;
        for (int i = 0; i < n; i++) {
            acc += dist2[i];
            if (acc >= r) { chosen = i; break; }
        }
        memcpy(centres + (size_t)c * nbands, pixels + (size_t)chosen * nbands,
               (size_t)nbands * sizeof(double));
    }
    G_free(dist2);
}

/* ── Read reference spectrum from a one-column-per-line CSV ────────── */
static double *read_spectrum_csv(const char *path, int nbands)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    double *spec = (double *)G_malloc((size_t)nbands * sizeof(double));
    int b = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && b < nbands) {
        if (line[0] == '#' || line[0] == '\n') continue;
        spec[b++] = atof(line);
    }
    fclose(fp);
    if (b < nbands) {
        G_warning(_("Spectrum CSV '%s' has %d values, expected %d"),
                  path, b, nbands);
        G_free(spec);
        return NULL;
    }
    return spec;
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_mode;
    struct Option  *opt_k, *opt_iter, *opt_seed;
    struct Option  *opt_spectrum, *opt_threshold, *opt_stats;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("classification"));
    G_add_keyword(_("k-means"));
    G_add_keyword(_("SAM"));
    G_add_keyword(_("geologic mapping"));
    module->label = _("Spectral classification of multi-band planetary rasters.");
    module->description =
        _("Two modes: 'kmeans' performs unsupervised k-means++ clustering "
          "and writes a geologic unit integer raster; 'sam' performs supervised "
          "Spectral Angle Mapper classification against a reference spectrum CSV "
          "and writes a binary match raster (1 = match, 0 = no match, NULL = "
          "invalid pixel). Reads bands named input.1, input.2, ...");

    opt_input = G_define_option();
    opt_input->key         = "input";
    opt_input->type        = TYPE_STRING;
    opt_input->required    = YES;
    opt_input->description = _("Base name of input band rasters (input.1, input.2, ...)");

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_mode = G_define_option();
    opt_mode->key         = "mode";
    opt_mode->type        = TYPE_STRING;
    opt_mode->required    = YES;
    opt_mode->options     = "kmeans,sam";
    opt_mode->description = _("Classification mode: kmeans (unsupervised) or sam (supervised)");

    opt_k = G_define_option();
    opt_k->key         = "k";
    opt_k->type        = TYPE_INTEGER;
    opt_k->required    = NO;
    opt_k->answer      = "5";
    opt_k->description = _("[kmeans] Number of spectral classes");

    opt_iter = G_define_option();
    opt_iter->key         = "iterations";
    opt_iter->type        = TYPE_INTEGER;
    opt_iter->required    = NO;
    opt_iter->answer      = "100";
    opt_iter->description = _("[kmeans] Maximum k-means iterations");

    opt_seed = G_define_option();
    opt_seed->key         = "seed";
    opt_seed->type        = TYPE_INTEGER;
    opt_seed->required    = NO;
    opt_seed->answer      = "0";
    opt_seed->description = _("[kmeans] Random seed (0 = use system time)");

    opt_spectrum = G_define_option();
    opt_spectrum->key         = "spectrum";
    opt_spectrum->type        = TYPE_STRING;
    opt_spectrum->required    = NO;
    opt_spectrum->description = _("[sam] Reference spectrum CSV (one reflectance value per line)");

    opt_threshold = G_define_option();
    opt_threshold->key         = "threshold";
    opt_threshold->type        = TYPE_DOUBLE;
    opt_threshold->required    = NO;
    opt_threshold->answer      = "0.1";
    opt_threshold->description = _("[sam] SAM angle threshold in radians (default 0.1 ≈ 5.7°)");

    opt_stats = G_define_option();
    opt_stats->key         = "stats";
    opt_stats->type        = TYPE_STRING;
    opt_stats->required    = NO;
    opt_stats->description = _("Output CSV: class centroids (kmeans) or SAM angle stats (sam)");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *inbase = opt_input->answer;
    const char *mode   = opt_mode->answer;
    int  k             = atoi(opt_k->answer);
    int  maxiter       = atoi(opt_iter->answer);
    int  seed_val      = atoi(opt_seed->answer);
    double threshold   = atof(opt_threshold->answer);

    /* ── Count input bands ─────────────────────────────────────────────── */
    int nbands = 0;
    char mapname[1024];
    for (int b = 1; b <= 100000; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b);
        if (!G_find_raster(mapname, "")) break;
        nbands++;
    }
    if (nbands < 2)
        G_fatal_error(_("Need at least 2 bands named '%s.1', '%s.2', …; found %d"),
                      inbase, inbase, nbands);
    G_message(_("Found %d bands for '%s'"), nbands, inbase);

    /* ── Mode-specific validation ──────────────────────────────────────── */
    double *ref_spectrum = NULL;
    if (strcmp(mode, "sam") == 0) {
        if (!opt_spectrum->answer)
            G_fatal_error(_("sam mode requires spectrum= (reference spectrum CSV)"));
        ref_spectrum = read_spectrum_csv(opt_spectrum->answer, nbands);
        if (!ref_spectrum)
            G_fatal_error(_("Cannot read reference spectrum from '%s'"),
                          opt_spectrum->answer);
    } else {
        if (k < 2 || k > 255)
            G_fatal_error(_("k must be between 2 and 255 (got %d)"), k);
    }

    /* ── Open input rasters ────────────────────────────────────────────── */
    int *fd_in = (int *)G_malloc((size_t)nbands * sizeof(int));
    for (int b = 0; b < nbands; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b + 1);
        fd_in[b] = Rast_open_old(mapname, "");
    }
    struct Cell_head reg;
    G_get_window(&reg);
    Rast_set_window(&reg);
    int nrows = reg.rows, ncols = reg.cols;

    DCELL **bufs = (DCELL **)G_malloc((size_t)nbands * sizeof(DCELL *));
    for (int b = 0; b < nbands; b++)
        bufs[b] = Rast_allocate_d_buf();

    /* ── SAM mode: one-pass classification ─────────────────────────────── */
    if (strcmp(mode, "sam") == 0) {
        int fd_out = Rast_open_new(opt_output->answer, CELL_TYPE);
        CELL *buf_out = Rast_allocate_c_buf();

        long  n_match = 0, n_total = 0;
        double angle_sum = 0.0;

        for (int row = 0; row < nrows; row++) {
            G_percent(row, nrows, 2);
            for (int b = 0; b < nbands; b++)
                Rast_get_d_row(fd_in[b], bufs[b], row);

            for (int c = 0; c < ncols; c++) {
                int valid = 1;
                for (int b = 0; b < nbands; b++)
                    if (Rast_is_d_null_value(&bufs[b][c])) { valid = 0; break; }
                if (!valid) {
                    Rast_set_c_null_value(&buf_out[c], 1);
                    continue;
                }
                double spec[4096]; /* nbands ≤ 4096 */
                for (int b = 0; b < nbands; b++)
                    spec[b] = bufs[b][c];
                double angle = sam_angle(nbands, spec, ref_spectrum);
                n_total++;
                angle_sum += angle;
                buf_out[c] = (angle <= threshold) ? 1 : 0;
                if (angle <= threshold) n_match++;
            }
            Rast_put_c_row(fd_out, buf_out);
        }
        G_percent(1, 1, 2);
        Rast_close(fd_out);
        G_free(buf_out);
        G_message(_("SAM classification: %ld/%ld pixels match (angle ≤ %.4f rad)"),
                  n_match, n_total, threshold);

        if (opt_stats->answer) {
            FILE *fp = fopen(opt_stats->answer, "w");
            if (fp) {
                fprintf(fp, "# p.spec.class sam stats\n");
                fprintf(fp, "threshold_rad,matched_pixels,total_pixels,match_pct,mean_angle_rad\n");
                fprintf(fp, "%.6f,%ld,%ld,%.4f,%.6f\n",
                        threshold, n_match, n_total,
                        (n_total > 0) ? 100.0 * n_match / n_total : 0.0,
                        (n_total > 0) ? angle_sum / n_total : 0.0);
                fclose(fp);
            }
        }
        G_free(ref_spectrum);

    /* ── k-means mode ──────────────────────────────────────────────────── */
    } else {
        /* Load all valid pixels into memory */
        G_message(_("Loading valid pixels for k-means ..."));
        long npix_cap = (long)nrows * ncols;
        double *pixels = (double *)G_malloc((size_t)npix_cap * nbands * sizeof(double));
        int    *row_idx = (int *)G_malloc((size_t)npix_cap * sizeof(int));
        int    *col_idx = (int *)G_malloc((size_t)npix_cap * sizeof(int));
        long    npix = 0;

        for (int row = 0; row < nrows; row++) {
            G_percent(row, nrows, 5);
            for (int b = 0; b < nbands; b++)
                Rast_get_d_row(fd_in[b], bufs[b], row);
            for (int c = 0; c < ncols; c++) {
                int valid = 1;
                for (int b = 0; b < nbands; b++)
                    if (Rast_is_d_null_value(&bufs[b][c])) { valid = 0; break; }
                if (!valid) continue;
                for (int b = 0; b < nbands; b++)
                    pixels[(size_t)npix * nbands + b] = bufs[b][c];
                row_idx[npix] = row;
                col_idx[npix] = c;
                npix++;
            }
        }
        G_percent(1, 1, 5);
        if (npix < k)
            G_fatal_error(_("Fewer valid pixels (%ld) than clusters k=%d"), npix, k);
        G_message(_("Loaded %ld valid pixels; initialising %d centres (k-means++) ..."),
                  npix, k);

        /* k-means++ initialisation */
        unsigned int seed = (seed_val == 0) ? (unsigned int)time(NULL) : (unsigned int)seed_val;
        double *centres  = (double *)G_malloc((size_t)k * nbands * sizeof(double));
        int    *labels   = (int    *)G_malloc((size_t)npix * sizeof(int));
        long   *counts   = (long   *)G_malloc((size_t)k * sizeof(long));
        double *new_cent = (double *)G_malloc((size_t)k * nbands * sizeof(double));

        kmeans_pp_init((int)npix, nbands, pixels, k, centres, &seed);

        /* k-means iterations */
        int changed = 1;
        for (int iter = 0; iter < maxiter && changed; iter++) {
            changed = 0;
            /* Assignment */
            for (long i = 0; i < npix; i++) {
                double best_d = 1e300;
                int    best_c = 0;
                for (int c = 0; c < k; c++) {
                    double d2 = 0.0;
                    for (int b = 0; b < nbands; b++) {
                        double diff = pixels[(size_t)i * nbands + b]
                                      - centres[(size_t)c * nbands + b];
                        d2 += diff * diff;
                    }
                    if (d2 < best_d) { best_d = d2; best_c = c; }
                }
                if (labels[i] != best_c) { labels[i] = best_c; changed++; }
            }
            /* Update centres */
            memset(new_cent, 0, (size_t)k * nbands * sizeof(double));
            memset(counts, 0, (size_t)k * sizeof(long));
            for (long i = 0; i < npix; i++) {
                int c = labels[i];
                counts[c]++;
                for (int b = 0; b < nbands; b++)
                    new_cent[(size_t)c * nbands + b] += pixels[(size_t)i * nbands + b];
            }
            for (int c = 0; c < k; c++) {
                if (counts[c] == 0) {
                    /* Reinitialise empty cluster to a random pixel */
                    long ri = (long)(rand_r(&seed) % npix);
                    memcpy(new_cent + (size_t)c * nbands,
                           pixels  + (size_t)ri * nbands,
                           (size_t)nbands * sizeof(double));
                    counts[c] = 1;
                } else {
                    for (int b = 0; b < nbands; b++)
                        new_cent[(size_t)c * nbands + b] /= counts[c];
                }
            }
            memcpy(centres, new_cent, (size_t)k * nbands * sizeof(double));
            G_verbose_message(_("  iter %d: %d pixels reassigned"), iter + 1, changed);
        }
        G_message(_("k-means converged; writing output ..."));

        /* Write stats CSV: centroid spectra */
        if (opt_stats->answer) {
            FILE *fp = fopen(opt_stats->answer, "w");
            if (fp) {
                fprintf(fp, "# p.spec.class kmeans centroids\n");
                fprintf(fp, "# k=%d  npixels=%ld\n", k, npix);
                fprintf(fp, "class,n_pixels");
                for (int b = 0; b < nbands; b++) fprintf(fp, ",band%d", b + 1);
                fprintf(fp, "\n");
                for (int c = 0; c < k; c++) {
                    fprintf(fp, "%d,%ld", c + 1, counts[c]);
                    for (int b = 0; b < nbands; b++)
                        fprintf(fp, ",%g", centres[(size_t)c * nbands + b]);
                    fprintf(fp, "\n");
                }
                fclose(fp);
                G_message(_("Centroid spectra written to '%s'"), opt_stats->answer);
            }
        }

        /* Write output: build label raster from scatter arrays */
        /* Allocate a 2D label array */
        int **label_grid = (int **)G_malloc((size_t)nrows * sizeof(int *));
        for (int r = 0; r < nrows; r++) {
            label_grid[r] = (int *)G_malloc((size_t)ncols * sizeof(int));
            for (int c = 0; c < ncols; c++) label_grid[r][c] = -1;
        }
        for (long i = 0; i < npix; i++)
            label_grid[row_idx[i]][col_idx[i]] = labels[i] + 1; /* 1-based */

        int fd_out = Rast_open_new(opt_output->answer, CELL_TYPE);
        CELL *buf_out = Rast_allocate_c_buf();
        for (int row = 0; row < nrows; row++) {
            for (int c = 0; c < ncols; c++) {
                if (label_grid[row][c] < 0)
                    Rast_set_c_null_value(&buf_out[c], 1);
                else
                    buf_out[c] = (CELL)label_grid[row][c];
            }
            Rast_put_c_row(fd_out, buf_out);
        }
        Rast_close(fd_out);
        G_free(buf_out);
        for (int r = 0; r < nrows; r++) G_free(label_grid[r]);
        G_free(label_grid);
        G_free(pixels); G_free(row_idx); G_free(col_idx);
        G_free(centres); G_free(labels); G_free(counts); G_free(new_cent);
    }

    /* ── Cleanup ──────────────────────────────────────────────────────── */
    for (int b = 0; b < nbands; b++) { Rast_close(fd_in[b]); G_free(bufs[b]); }
    G_free(fd_in); G_free(bufs);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);

    G_message(_("p.spec.class: %s classification complete → '%s'"),
              mode, opt_output->answer);
    return EXIT_SUCCESS;
}
