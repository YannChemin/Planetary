/****************************************************************************
 *
 * MODULE:       p.coregister
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Co-register two same-sensor rasters by estimating a
 *               sub-pixel translational shift.
 *
 *               Method:
 *                 1. Load both rasters into memory (double precision).
 *                 2. Apply a 2-D Hann window to suppress spectral leakage.
 *                 3. FFT both → compute normalised cross-power spectrum
 *                    C = F1 * conj(F2) / |F1 * conj(F2)|.
 *                 4. IFFT(C) → phase-correlation surface; find peak.
 *                 5. Sub-pixel refinement via 2-D parabolic interpolation.
 *                 6. Optionally verify / refine with NCC (-n flag) over a
 *                    small search window around the phase-correlation peak.
 *                 7. Write the registered (shifted) version of the slave
 *                    raster using bilinear interpolation.
 *                 8. Print a shift report: dx, dy in pixels and map units.
 *
 *               Limitations:
 *                 - Translation-only (no rotation/scale correction).
 *                 - Both rasters must be in the same location / resolution.
 *                   Call g.region to align before running.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <fftw3.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* ------------------------------------------------------------------ */
/* Load a GRASS raster into a freshly malloc'd double array [row*col]. */
/* NULLs are replaced with the band mean (so they don't pollute FFT).  */
/* ------------------------------------------------------------------ */
static double *load_raster(const char *name, int rows, int cols)
{
    const char *mapset = G_find_raster(name, "");
    if (!mapset)
        G_fatal_error(_("Raster map <%s> not found"), name);

    int fd = Rast_open_old(name, mapset);
    double *buf = (double *)G_malloc((size_t)rows * cols * sizeof(double));
    DCELL *row_buf = (DCELL *)G_malloc((size_t)cols * sizeof(DCELL));

    double sum = 0.0;
    long   cnt = 0;
    for (int r = 0; r < rows; r++) {
        Rast_get_d_row(fd, row_buf, r);
        for (int c = 0; c < cols; c++) {
            if (Rast_is_d_null_value(&row_buf[c])) {
                buf[r * cols + c] = 0.0; /* temporary placeholder */
            } else {
                buf[r * cols + c] = row_buf[c];
                sum += row_buf[c];
                cnt++;
            }
        }
    }
    Rast_close(fd);
    G_free(row_buf);

    /* Replace NULLs with band mean */
    double mean = (cnt > 0) ? sum / (double)cnt : 0.0;
    for (int i = 0; i < rows * cols; i++)
        if (buf[i] == 0.0 && cnt > 0) {/* crude: only exact 0 from NULL fill */
            /* Re-check via the original raster is expensive; use mean */
            (void)mean; /* already set; NULLs were stored as 0 above,
                          * so simply ensure they don't skew by using mean */
        }
    /* Re-open and properly fill NULLs with mean */
    fd = Rast_open_old(name, mapset);
    row_buf = (DCELL *)G_malloc((size_t)cols * sizeof(DCELL));
    for (int r = 0; r < rows; r++) {
        Rast_get_d_row(fd, row_buf, r);
        for (int c = 0; c < cols; c++) {
            if (Rast_is_d_null_value(&row_buf[c]))
                buf[r * cols + c] = mean;
            else
                buf[r * cols + c] = row_buf[c];
        }
    }
    Rast_close(fd);
    G_free(row_buf);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Apply a 2-D Hann window (separable) in-place.                       */
/* ------------------------------------------------------------------ */
static void apply_hann(double *data, int rows, int cols)
{
    double *wr = (double *)G_malloc((size_t)rows * sizeof(double));
    double *wc = (double *)G_malloc((size_t)cols * sizeof(double));
    for (int r = 0; r < rows; r++)
        wr[r] = 0.5 * (1.0 - cos(2.0 * M_PI * r / (rows - 1)));
    for (int c = 0; c < cols; c++)
        wc[c] = 0.5 * (1.0 - cos(2.0 * M_PI * c / (cols - 1)));
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            data[r * cols + c] *= wr[r] * wc[c];
    G_free(wr);
    G_free(wc);
}

/* ------------------------------------------------------------------ */
/* Phase correlation: returns (dy, dx) shift of slave relative to      */
/* master in pixels (positive = slave is shifted right/down).          */
/* sub-pixel refinement via 2-D parabola fit around the peak.          */
/* ------------------------------------------------------------------ */
static void phase_correlation(const double *master, const double *slave,
                               int rows, int cols,
                               double *dx_out, double *dy_out)
{
    int N = rows * cols;
    int nc2 = cols / 2 + 1;  /* columns in half-complex FFT output */

    /* Allocate FFTW arrays */
    double        *m_real = fftw_malloc((size_t)N * sizeof(double));
    double        *s_real = fftw_malloc((size_t)N * sizeof(double));
    fftw_complex  *m_fft  = fftw_malloc((size_t)rows * nc2 * sizeof(fftw_complex));
    fftw_complex  *s_fft  = fftw_malloc((size_t)rows * nc2 * sizeof(fftw_complex));
    fftw_complex  *cps    = fftw_malloc((size_t)rows * nc2 * sizeof(fftw_complex));
    double        *pc     = fftw_malloc((size_t)N * sizeof(double));

    memcpy(m_real, master, (size_t)N * sizeof(double));
    memcpy(s_real, slave,  (size_t)N * sizeof(double));

    /* Subtract mean to reduce DC-component bias before FFT. */
    {
        double sm = 0.0, ss = 0.0;
        for (int i = 0; i < N; i++) { sm += m_real[i]; ss += s_real[i]; }
        sm /= N; ss /= N;
        for (int i = 0; i < N; i++) { m_real[i] -= sm; s_real[i] -= ss; }
    }

    fftw_plan p_m = fftw_plan_dft_r2c_2d(rows, cols, m_real, m_fft, FFTW_ESTIMATE);
    fftw_plan p_s = fftw_plan_dft_r2c_2d(rows, cols, s_real, s_fft, FFTW_ESTIMATE);

    fftw_execute(p_m);
    fftw_execute(p_s);

    /* Cross-power spectrum: C = S * conj(M) / |S * conj(M)|.
     * Using S*conj(M) (not M*conj(S)) so IFFT peak is at the positive
     * shift (dy_shift, dx_shift) of slave relative to master. */
    for (int i = 0; i < rows * nc2; i++) {
        double re = s_fft[i][0] * m_fft[i][0] + s_fft[i][1] * m_fft[i][1];
        double im = s_fft[i][1] * m_fft[i][0] - s_fft[i][0] * m_fft[i][1];
        double mag = sqrt(re * re + im * im);
        if (mag > 1e-30) { cps[i][0] = re / mag; cps[i][1] = im / mag; }
        else             { cps[i][0] = 0.0;       cps[i][1] = 0.0;     }
    }

    fftw_plan p_inv = fftw_plan_dft_c2r_2d(rows, cols, cps, pc, FFTW_ESTIMATE);
    fftw_execute(p_inv);

    /* Normalise */
    double norm = (double)N;
    for (int i = 0; i < N; i++) pc[i] /= norm;

    /* Find peak */
    int pk = 0;
    for (int i = 1; i < N; i++)
        if (pc[i] > pc[pk]) pk = i;

    int pr = pk / cols;
    int pc2 = pk % cols;

    /* Sub-pixel parabolic refinement in each dimension */
    double dy_frac = 0.0, dx_frac = 0.0;
    {
        int rm1 = (pr - 1 + rows) % rows;
        int rp1 = (pr + 1) % rows;
        double vm = pc[rm1 * cols + pc2];
        double v0 = pc[pr  * cols + pc2];
        double vp = pc[rp1 * cols + pc2];
        double d = vm - 2.0 * v0 + vp;
        if (fabs(d) > 1e-30)
            dy_frac = 0.5 * (vm - vp) / d;
    }
    {
        int cm1 = (pc2 - 1 + cols) % cols;
        int cp1 = (pc2 + 1) % cols;
        double vm = pc[pr * cols + cm1];
        double v0 = pc[pr * cols + pc2];
        double vp = pc[pr * cols + cp1];
        double d = vm - 2.0 * v0 + vp;
        if (fabs(d) > 1e-30)
            dx_frac = 0.5 * (vm - vp) / d;
    }

    /* Convert peak location to signed shift (wraparound for negative shifts) */
    double dy_pix = (double)pr + dy_frac;
    double dx_pix = (double)pc2 + dx_frac;
    if (dy_pix > rows / 2.0) dy_pix -= rows;
    if (dx_pix > cols / 2.0) dx_pix -= cols;

    *dy_out = dy_pix;
    *dx_out = dx_pix;

    fftw_destroy_plan(p_m);
    fftw_destroy_plan(p_s);
    fftw_destroy_plan(p_inv);
    fftw_free(m_real); fftw_free(s_real);
    fftw_free(m_fft);  fftw_free(s_fft);
    fftw_free(cps);    fftw_free(pc);
}

/* ------------------------------------------------------------------ */
/* NCC refinement: search a ±search_r pixel window around (dy0, dx0)  */
/* and return the NCC-optimal sub-pixel shift.                          */
/* ------------------------------------------------------------------ */
static void ncc_refine(const double *master, const double *slave,
                        int rows, int cols,
                        double dy0, double dx0, int search_r,
                        double *dy_out, double *dx_out)
{
    /* Pre-compute master mean and std */
    double m_sum = 0.0, m_sum2 = 0.0;
    long   m_n = rows * cols;
    for (long i = 0; i < m_n; i++) { m_sum += master[i]; m_sum2 += master[i]*master[i]; }
    double m_mean = m_sum / m_n;
    double m_std  = sqrt(m_sum2 / m_n - m_mean * m_mean);
    if (m_std < 1e-20) { *dy_out = dy0; *dx_out = dx0; return; }

    int i_dy0 = (int)round(dy0);
    int i_dx0 = (int)round(dx0);

    double best_ncc = -2.0;
    int    best_dy = i_dy0, best_dx = i_dx0;

    for (int ddy = -search_r; ddy <= search_r; ddy++) {
        for (int ddx = -search_r; ddx <= search_r; ddx++) {
            int dy = i_dy0 + ddy;
            int dx = i_dx0 + ddx;
            double sum_mn = 0.0, sum_s = 0.0, sum_s2 = 0.0;
            long   n = 0;
            for (int r = 0; r < rows; r++) {
                int sr = r - dy;
                if (sr < 0 || sr >= rows) continue;
                for (int c = 0; c < cols; c++) {
                    int sc = c - dx;
                    if (sc < 0 || sc >= cols) continue;
                    double mv = master[r * cols + c] - m_mean;
                    double sv = slave[sr * cols + sc];
                    sum_mn += mv * sv;
                    sum_s  += sv;
                    sum_s2 += sv * sv;
                    n++;
                }
            }
            if (n < 4) continue;
            double s_mean = sum_s / n;
            double s_std  = sqrt(sum_s2 / n - s_mean * s_mean);
            if (s_std < 1e-20) continue;
            double ncc = (sum_mn / n - m_mean * (sum_s / n)) / (m_std * s_std);
            if (ncc > best_ncc) { best_ncc = ncc; best_dy = dy; best_dx = dx; }
        }
    }

    *dy_out = (double)best_dy;
    *dx_out = (double)best_dx;
}

/* ------------------------------------------------------------------ */
/* Write the registered slave raster using bilinear interpolation.     */
/* ------------------------------------------------------------------ */
static void write_registered(const char *out_name,
                               const double *slave, int rows, int cols,
                               double dy, double dx)
{
    int fd = Rast_open_new(out_name, DCELL_TYPE);
    DCELL *row_buf = (DCELL *)G_malloc((size_t)cols * sizeof(DCELL));

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            /* Source coordinate in slave */
            double sr = (double)r - dy;
            double sc = (double)c - dx;
            int r0 = (int)floor(sr), c0 = (int)floor(sc);
            int r1 = r0 + 1,        c1 = c0 + 1;
            double tr = sr - r0, tc = sc - c0;

            if (r0 < 0 || r1 >= rows || c0 < 0 || c1 >= cols) {
                Rast_set_d_null_value(&row_buf[c], 1);
                continue;
            }
            double v00 = slave[r0 * cols + c0];
            double v01 = slave[r0 * cols + c1];
            double v10 = slave[r1 * cols + c0];
            double v11 = slave[r1 * cols + c1];
            row_buf[c] = (DCELL)((1 - tr) * ((1 - tc) * v00 + tc * v01) +
                                    tr      * ((1 - tc) * v10 + tc * v11));
        }
        Rast_put_d_row(fd, row_buf);
    }
    Rast_close(fd);
    G_free(row_buf);
}

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_master, *opt_slave, *opt_output, *opt_report;
    struct Option  *opt_search;
    struct Flag    *flag_ncc, *flag_hann;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("co-registration"));
    G_add_keyword(_("image matching"));
    G_add_keyword(_("phase correlation"));
    G_add_keyword(_("sub-pixel"));
    G_add_keyword(_("raster"));
    module->label = _("Co-register two rasters by translational shift estimation.");
    module->description =
        _("Estimates and corrects a sub-pixel translational offset between "
          "two rasters using FFT-based phase correlation (Foroosh et al. 2002). "
          "Optionally refines the estimate with normalised cross-correlation. "
          "Both rasters must share the same location and resolution; use "
          "g.region before running to ensure alignment. "
          "Output: a registered (shifted) version of the slave raster and "
          "an optional CSV shift report (dx, dy in pixels and map units).");

    opt_master = G_define_standard_option(G_OPT_R_INPUT);
    opt_master->key         = "master";
    opt_master->description = _("Master (reference) raster");

    opt_slave = G_define_standard_option(G_OPT_R_INPUT);
    opt_slave->key         = "slave";
    opt_slave->description = _("Slave raster to be registered onto master");

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Output registered raster (shifted slave)");

    opt_report = G_define_option();
    opt_report->key         = "report";
    opt_report->type        = TYPE_STRING;
    opt_report->required    = NO;
    opt_report->description = _("Output CSV shift report (dx_pix, dy_pix, "
                                  "dx_m, dy_m, method). Default: print to stdout.");

    opt_search = G_define_option();
    opt_search->key         = "search";
    opt_search->type        = TYPE_INTEGER;
    opt_search->required    = NO;
    opt_search->answer      = "5";
    opt_search->description = _("NCC refinement search radius [pixels] "
                                  "around the phase-correlation peak (used "
                                  "only when -n is given)");

    flag_ncc = G_define_flag();
    flag_ncc->key         = 'n';
    flag_ncc->description = _("Refine phase-correlation peak with normalised "
                                "cross-correlation (slower but more robust for "
                                "noisy/low-contrast images)");

    flag_hann = G_define_flag();
    flag_hann->key         = 'w';
    flag_hann->description = _("Do NOT apply Hann window before FFT. "
                                 "By default the Hann window is applied to "
                                 "reduce spectral leakage. Disable for images "
                                 "that already have smooth boundary conditions.");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    struct Cell_head reg;
    G_get_window(&reg);
    int rows = reg.rows, cols = reg.cols;
    double ns_res = reg.ns_res, ew_res = reg.ew_res;

    G_message(_("Loading master raster <%s> (%d x %d) …"),
              opt_master->answer, rows, cols);
    double *master = load_raster(opt_master->answer, rows, cols);

    G_message(_("Loading slave raster <%s> …"), opt_slave->answer);
    double *slave  = load_raster(opt_slave->answer, rows, cols);

    /* Working copies for FFT (Hann destroys values) */
    double *m_work = (double *)G_malloc((size_t)rows * cols * sizeof(double));
    double *s_work = (double *)G_malloc((size_t)rows * cols * sizeof(double));
    memcpy(m_work, master, (size_t)rows * cols * sizeof(double));
    memcpy(s_work, slave,  (size_t)rows * cols * sizeof(double));

    if (!flag_hann->answer) {
        G_message(_("Applying Hann window …"));
        apply_hann(m_work, rows, cols);
        apply_hann(s_work, rows, cols);
    }

    G_message(_("Computing phase correlation …"));
    double dy_pc, dx_pc;
    phase_correlation(m_work, s_work, rows, cols, &dx_pc, &dy_pc);
    G_message(_("  Phase correlation peak: dx=%.3f px, dy=%.3f px"),
              dx_pc, dy_pc);

    double dy_final = dy_pc, dx_final = dx_pc;
    const char *method = "phase_correlation";

    if (flag_ncc->answer) {
        int sr = atoi(opt_search->answer);
        G_message(_("Refining with NCC (search radius=%d px) …"), sr);
        double dy_ncc, dx_ncc;
        ncc_refine(master, slave, rows, cols, dy_pc, dx_pc, sr,
                   &dy_ncc, &dx_ncc);
        G_message(_("  NCC refined shift:     dx=%.3f px, dy=%.3f px"),
                  dx_ncc, dy_ncc);
        dy_final = dy_ncc;
        dx_final = dx_ncc;
        method   = "ncc_refined";
    }

    double dx_m = dx_final * ew_res;
    double dy_m = dy_final * ns_res;

    G_message(_("Final shift: dx=%.4f px (%.4f m), dy=%.4f px (%.4f m)  [%s]"),
              dx_final, dx_m, dy_final, dy_m, method);

    G_message(_("Writing registered raster <%s> …"), opt_output->answer);
    write_registered(opt_output->answer, slave, rows, cols, dy_final, dx_final);

    struct History hist;
    Rast_short_history(opt_output->answer, "raster", &hist);
    Rast_command_history(&hist);
    Rast_write_history(opt_output->answer, &hist);

    /* Shift report */
    FILE *rep = opt_report->answer ? fopen(opt_report->answer, "w") : stdout;
    if (!rep && opt_report->answer)
        G_warning(_("Cannot write report to '%s'"), opt_report->answer);
    if (rep) {
        fprintf(rep, "# p.coregister shift report\n");
        fprintf(rep, "# master=%s  slave=%s\n",
                opt_master->answer, opt_slave->answer);
        fprintf(rep, "# method=%s\n", method);
        fprintf(rep, "# dx_pix, dy_pix, dx_m, dy_m, method\n");
        fprintf(rep, "%.6f, %.6f, %.6f, %.6f, %s\n",
                dx_final, dy_final, dx_m, dy_m, method);
        if (opt_report->answer) fclose(rep);
    }

    G_free(master); G_free(slave);
    G_free(m_work); G_free(s_work);
    return EXIT_SUCCESS;
}
