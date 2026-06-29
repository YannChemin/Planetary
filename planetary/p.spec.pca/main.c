/****************************************************************************
 *
 * MODULE:       p.spec.pca
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Principal Component Analysis of a multi-band planetary raster
 *               group. Computes covariance-matrix PCA (or correlation-matrix
 *               PCA with -s) via cyclic Jacobi eigendecomposition and writes
 *               the requested number of PC score rasters plus an eigenvalue CSV.
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
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* ── Cyclic Jacobi symmetric eigendecomposition ────────────────────────────
 * A[n×n] stored row-major.  On entry: symmetric matrix.
 * On return: eigenvalues in A[i*n+i], eigenvectors in columns of V (V must
 * be initialised to the identity by the caller).
 * Only the upper triangle of A is read and updated.
 */
static void jacobi_eigen(int n, double *A, double *V)
{
    const double tol      = 1e-14;
    const int    maxsweep = 80;

    for (int sw = 0; sw < maxsweep; sw++) {
        /* Check convergence: sum of squared off-diagonal elements */
        double off2 = 0.0;
        for (int p = 0; p < n - 1; p++)
            for (int q = p + 1; q < n; q++)
                off2 += A[p * n + q] * A[p * n + q];
        if (off2 < tol)
            break;

        for (int p = 0; p < n - 1; p++) {
            for (int q = p + 1; q < n; q++) {
                double apq = A[p * n + q];
                if (fabs(apq) < tol)
                    continue;
                double app = A[p * n + p];
                double aqq = A[q * n + q];
                double tau = (aqq - app) / (2.0 * apq);
                double t   = (tau >= 0.0)
                             ?  1.0 / (tau + sqrt(1.0 + tau * tau))
                             : -1.0 / (-tau + sqrt(1.0 + tau * tau));
                double c   = 1.0 / sqrt(1.0 + t * t);
                double s   = t * c;

                A[p * n + p] = app - t * apq;
                A[q * n + q] = aqq + t * apq;
                A[p * n + q] = A[q * n + p] = 0.0;

                for (int r = 0; r < n; r++) {
                    if (r == p || r == q)
                        continue;
                    double arp = (r < p) ? A[r * n + p] : A[p * n + r];
                    double arq = (r < q) ? A[r * n + q] : A[q * n + r];
                    double narp =  c * arp - s * arq;
                    double narq =  s * arp + c * arq;
                    if (r < p) A[r * n + p] = narp; else A[p * n + r] = narp;
                    if (r < q) A[r * n + q] = narq; else A[q * n + r] = narq;
                }

                for (int r = 0; r < n; r++) {
                    double vrp = V[r * n + p];
                    double vrq = V[r * n + q];
                    V[r * n + p] =  c * vrp - s * vrq;
                    V[r * n + q] =  s * vrp + c * vrq;
                }
            }
        }
    }
}

/* Sort eigenvalues descending; reorder V columns accordingly */
static void sort_eigen_desc(int n, double *evals, double *V)
{
    for (int i = 0; i < n - 1; i++) {
        int imax = i;
        for (int j = i + 1; j < n; j++)
            if (evals[j] > evals[imax])
                imax = j;
        if (imax == i)
            continue;
        double tmp = evals[i]; evals[i] = evals[imax]; evals[imax] = tmp;
        for (int r = 0; r < n; r++) {
            tmp = V[r * n + i]; V[r * n + i] = V[r * n + imax]; V[r * n + imax] = tmp;
        }
    }
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_ncomps, *opt_stats;
    struct Flag    *flag_std;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("PCA"));
    G_add_keyword(_("principal component analysis"));
    G_add_keyword(_("hyperspectral"));
    module->label = _("Principal Component Analysis of a multi-band planetary raster.");
    module->description =
        _("Computes PCA via cyclic Jacobi eigendecomposition of the pixel "
          "covariance matrix (or correlation matrix with -s). Reads bands "
          "named input.1, input.2, ... and writes PC score rasters output.1, "
          "output.2, ... in descending variance order. Eigenvalues, cumulative "
          "variance explained, and eigenvectors are written to a CSV file.");

    opt_input = G_define_option();
    opt_input->key         = "input";
    opt_input->type        = TYPE_STRING;
    opt_input->required    = YES;
    opt_input->description = _("Base name of input band rasters (input.1, input.2, ...)");

    opt_output = G_define_option();
    opt_output->key         = "output";
    opt_output->type        = TYPE_STRING;
    opt_output->required    = YES;
    opt_output->description = _("Base name for output PC score rasters (output.1, output.2, ...)");

    opt_ncomps = G_define_option();
    opt_ncomps->key         = "ncomps";
    opt_ncomps->type        = TYPE_INTEGER;
    opt_ncomps->required    = NO;
    opt_ncomps->answer      = "0";
    opt_ncomps->description = _("Number of PC components to write (0 = all bands)");

    opt_stats = G_define_option();
    opt_stats->key         = "stats";
    opt_stats->type        = TYPE_STRING;
    opt_stats->required    = NO;
    opt_stats->description = _("Output CSV file for eigenvalues, variance explained, and eigenvectors");

    flag_std = G_define_flag();
    flag_std->key         = 's';
    flag_std->description = _("Standardise bands (correlation-matrix PCA): "
                               "divide each band by its standard deviation before PCA. "
                               "Recommended when bands span very different value ranges.");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *inbase  = opt_input->answer;
    const char *outbase = opt_output->answer;
    int         ncomps  = atoi(opt_ncomps->answer);
    int         do_std  = flag_std->answer;

    /* ── Count input bands ─────────────────────────────────────────────── */
    int   nbands = 0;
    char  mapname[1024];
    for (int b = 1; b <= 100000; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b);
        if (!G_find_raster(mapname, ""))
            break;
        nbands++;
    }
    if (nbands < 2)
        G_fatal_error(_("Need at least 2 bands named '%s.1', '%s.2', …; found %d"),
                      inbase, inbase, nbands);
    G_message(_("Found %d bands for '%s'"), nbands, inbase);

    if (ncomps <= 0 || ncomps > nbands)
        ncomps = nbands;

    /* ── Open input rasters ────────────────────────────────────────────── */
    int *fd_in = (int *)G_malloc((size_t)nbands * sizeof(int));
    for (int b = 0; b < nbands; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b + 1);
        fd_in[b] = Rast_open_old(mapname, "");
    }

    struct Cell_head reg;
    G_get_window(&reg);
    int nrows = reg.rows;
    int ncols = reg.cols;

    DCELL **bufs = (DCELL **)G_malloc((size_t)nbands * sizeof(DCELL *));
    for (int b = 0; b < nbands; b++)
        bufs[b] = Rast_allocate_d_buf();

    /* ── Pass 1: per-band mean and pixel count ─────────────────────────── */
    G_message(_("Pass 1: computing per-band mean ..."));
    double *mean   = (double *)G_calloc((size_t)nbands, sizeof(double));
    double *stddev = (double *)G_calloc((size_t)nbands, sizeof(double));
    long    npix   = 0;  /* count of valid (all-bands-valid) pixels */

    /* First accumulate sum per band counting only pixels where every band valid */
    long   *nvalid_b = (long *)G_calloc((size_t)nbands, sizeof(long));
    double *sum_b    = (double *)G_calloc((size_t)nbands, sizeof(double));

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 5);
        for (int b = 0; b < nbands; b++)
            Rast_get_d_row(fd_in[b], bufs[b], row);
        for (int c = 0; c < ncols; c++) {
            int valid = 1;
            for (int b = 0; b < nbands; b++)
                if (Rast_is_d_null_value(&bufs[b][c])) { valid = 0; break; }
            if (!valid) continue;
            npix++;
            for (int b = 0; b < nbands; b++) {
                sum_b[b] += bufs[b][c];
                nvalid_b[b]++;
            }
        }
    }
    G_percent(1, 1, 5);
    if (npix < (long)nbands)
        G_fatal_error(_("Too few valid pixels (%ld) for %d-band PCA"), npix, nbands);
    for (int b = 0; b < nbands; b++)
        mean[b] = (nvalid_b[b] > 0) ? sum_b[b] / nvalid_b[b] : 0.0;
    G_free(sum_b); G_free(nvalid_b);

    /* ── Pass 2: covariance matrix (upper triangle, row-major) ─────────── */
    G_message(_("Pass 2: computing covariance matrix (%d x %d) ..."), nbands, nbands);
    double *cov = (double *)G_calloc((size_t)nbands * nbands, sizeof(double));

    /* If standardising, accumulate variance first */
    if (do_std) {
        double *var_b = (double *)G_calloc((size_t)nbands, sizeof(double));
        long   *cnt_b = (long   *)G_calloc((size_t)nbands, sizeof(long));
        for (int row = 0; row < nrows; row++) {
            for (int b = 0; b < nbands; b++)
                Rast_get_d_row(fd_in[b], bufs[b], row);
            for (int c = 0; c < ncols; c++) {
                int valid = 1;
                for (int b = 0; b < nbands; b++)
                    if (Rast_is_d_null_value(&bufs[b][c])) { valid = 0; break; }
                if (!valid) continue;
                for (int b = 0; b < nbands; b++) {
                    double d = bufs[b][c] - mean[b];
                    var_b[b] += d * d;
                    cnt_b[b]++;
                }
            }
        }
        for (int b = 0; b < nbands; b++)
            stddev[b] = (cnt_b[b] > 1 && var_b[b] > 0.0)
                        ? sqrt(var_b[b] / (cnt_b[b] - 1)) : 1.0;
        G_free(var_b); G_free(cnt_b);
    } else {
        for (int b = 0; b < nbands; b++)
            stddev[b] = 1.0;
    }

    /* Accumulate covariance using centered (and optionally standardised) values */
    double *centered = (double *)G_malloc((size_t)nbands * sizeof(double));
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
                centered[b] = (bufs[b][c] - mean[b]) / stddev[b];
            /* Upper triangle of outer product */
            for (int b1 = 0; b1 < nbands; b1++) {
                double c1 = centered[b1];
                for (int b2 = b1; b2 < nbands; b2++)
                    cov[b1 * nbands + b2] += c1 * centered[b2];
            }
        }
    }
    G_percent(1, 1, 5);
    G_free(centered);

    /* Normalise to sample covariance and mirror to lower triangle */
    double norm = (npix > 1) ? 1.0 / (npix - 1) : 1.0;
    for (int b1 = 0; b1 < nbands; b1++)
        for (int b2 = b1; b2 < nbands; b2++) {
            cov[b1 * nbands + b2] *= norm;
            cov[b2 * nbands + b1]  = cov[b1 * nbands + b2];
        }

    /* ── Eigendecomposition ────────────────────────────────────────────── */
    G_message(_("Eigendecomposition of %d×%d covariance matrix (Jacobi) ..."),
              nbands, nbands);
    double *V = (double *)G_calloc((size_t)nbands * nbands, sizeof(double));
    /* Initialise V = identity */
    for (int b = 0; b < nbands; b++)
        V[b * nbands + b] = 1.0;
    jacobi_eigen(nbands, cov, V);

    /* Extract eigenvalues from diagonal of cov */
    double *evals = (double *)G_malloc((size_t)nbands * sizeof(double));
    for (int b = 0; b < nbands; b++)
        evals[b] = cov[b * nbands + b];
    G_free(cov);

    sort_eigen_desc(nbands, evals, V);

    /* ── Write stats CSV ───────────────────────────────────────────────── */
    double total_var = 0.0;
    for (int b = 0; b < nbands; b++)
        total_var += (evals[b] > 0.0) ? evals[b] : 0.0;

    if (opt_stats->answer) {
        FILE *fp = fopen(opt_stats->answer, "w");
        if (!fp)
            G_warning(_("Cannot write stats CSV '%s'"), opt_stats->answer);
        else {
            fprintf(fp, "# p.spec.pca eigenvalue report\n");
            fprintf(fp, "# input=%s  nbands=%d  npixels=%ld  standardised=%s\n",
                    inbase, nbands, npix, do_std ? "yes" : "no");
            fprintf(fp, "# PC,eigenvalue,variance_pct,cumulative_pct");
            for (int b = 0; b < nbands; b++)
                fprintf(fp, ",evec_band%d", b + 1);
            fprintf(fp, "\n");
            double cumvar = 0.0;
            for (int k = 0; k < nbands; k++) {
                double ev   = evals[k];
                double vpct = (total_var > 0.0) ? 100.0 * ev / total_var : 0.0;
                cumvar += vpct;
                fprintf(fp, "%d,%g,%.4f,%.4f", k + 1, ev, vpct, cumvar);
                for (int b = 0; b < nbands; b++)
                    fprintf(fp, ",%g", V[b * nbands + k]);
                fprintf(fp, "\n");
            }
            fclose(fp);
            G_message(_("Eigenvalue table written to '%s'"), opt_stats->answer);
        }
    } else {
        /* Print top-10 summary to log */
        double cumvar = 0.0;
        int    nprint = (nbands < 10) ? nbands : 10;
        G_message(_("Top %d PCs:"), nprint);
        for (int k = 0; k < nprint; k++) {
            double vpct = (total_var > 0.0) ? 100.0 * evals[k] / total_var : 0.0;
            cumvar += vpct;
            G_message(_("  PC%d: eigenvalue=%.4g  %.2f%%  (cumulative %.2f%%)"),
                      k + 1, evals[k], vpct, cumvar);
        }
    }

    /* ── Pass 3: project pixels → write PC rasters ─────────────────────── */
    G_message(_("Pass 3: writing %d PC rasters ..."), ncomps);

    /* Open output rasters */
    int *fd_out = (int *)G_malloc((size_t)ncomps * sizeof(int));
    for (int k = 0; k < ncomps; k++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", outbase, k + 1);
        fd_out[k] = Rast_open_new(mapname, DCELL_TYPE);
    }
    Rast_set_window(&reg);

    DCELL **bufs_out = (DCELL **)G_malloc((size_t)ncomps * sizeof(DCELL *));
    for (int k = 0; k < ncomps; k++)
        bufs_out[k] = Rast_allocate_d_buf();

    double *spectrum = (double *)G_malloc((size_t)nbands * sizeof(double));

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        for (int b = 0; b < nbands; b++)
            Rast_get_d_row(fd_in[b], bufs[b], row);

        for (int c = 0; c < ncols; c++) {
            int valid = 1;
            for (int b = 0; b < nbands; b++)
                if (Rast_is_d_null_value(&bufs[b][c])) { valid = 0; break; }

            if (!valid) {
                for (int k = 0; k < ncomps; k++)
                    Rast_set_d_null_value(&bufs_out[k][c], 1);
                continue;
            }

            /* Centered (and optionally standardised) spectrum */
            for (int b = 0; b < nbands; b++)
                spectrum[b] = (bufs[b][c] - mean[b]) / stddev[b];

            /* Project onto each eigenvector: score_k = V[:,k] · spectrum */
            for (int k = 0; k < ncomps; k++) {
                double score = 0.0;
                for (int b = 0; b < nbands; b++)
                    score += V[b * nbands + k] * spectrum[b];
                bufs_out[k][c] = (DCELL)score;
            }
        }

        for (int k = 0; k < ncomps; k++)
            Rast_put_d_row(fd_out[k], bufs_out[k]);
    }
    G_percent(1, 1, 2);

    /* ── Cleanup ──────────────────────────────────────────────────────── */
    for (int b = 0; b < nbands; b++) { Rast_close(fd_in[b]); G_free(bufs[b]); }
    G_free(fd_in); G_free(bufs);
    G_free(mean); G_free(stddev); G_free(evals); G_free(V); G_free(spectrum);

    for (int k = 0; k < ncomps; k++) {
        Rast_close(fd_out[k]);
        G_free(bufs_out[k]);
        snprintf(mapname, sizeof(mapname), "%s.%d", outbase, k + 1);
        Rast_short_history(mapname, "raster", &history);
        Rast_command_history(&history);
        Rast_write_history(mapname, &history);
    }
    G_free(fd_out); G_free(bufs_out);

    G_message(_("p.spec.pca: wrote %d PC rasters '%s.1' ... '%s.%d'"),
              ncomps, outbase, outbase, ncomps);
    return EXIT_SUCCESS;
}
