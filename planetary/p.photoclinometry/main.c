/****************************************************************************
 *
 * MODULE:       p.photoclinometry
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Shape-from-shading photoclinometry using a planetary
 *               photometric model (Hapke, Minnaert, etc.) and a seed DEM.
 *
 *               Implements the Horn-Brooks (1986) iterative gradient-domain
 *               algorithm: updates the surface gradient field (p,q) to
 *               satisfy the photometric equation at every pixel, then
 *               integrates (p,q) to recover height.
 *
 *               References:
 *                 Horn & Brooks (1986) "The variational approach to shape
 *                   from shading", CVGIP 33, 174-208.
 *                 Kirk (1987) "A fast finite-element algorithm for two-
 *                   dimensional photoclinometry", PhD thesis, Caltech.
 *                 Lohse et al. (2006) "Derivation of planetary topography
 *                   using multiscale photoclinometry", P&SS 54, 661-674.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "../../libs/p_photomodel/p_photomodel.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static inline double clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/* Evaluate normalised brightness f(p,q)/f(0,0,0) for a surface gradient.
 * Returns 0 when the surface faces away from the sun (cos_i <= 0).
 */
static double eval_f(const PPhotoModel *m, double f_std,
                     double p, double q,
                     double sx, double sy, double sz, double g_deg)
{
    double N = sqrt(p * p + q * q + 1.0);
    double cos_i = (-p * sx - q * sy + sz) / N;
    if (cos_i <= 0.0)
        return 0.0;
    double cos_e = 1.0 / N;                        /* nadir viewing */
    double i_deg = acos(clamp01(cos_i)) * (180.0 / M_PI);
    double e_deg = acos(clamp01(cos_e)) * (180.0 / M_PI);
    return p_photomodel_eval(m, g_deg, i_deg, e_deg) / f_std;
}

/* ------------------------------------------------------------------ */
/* Horn-Brooks one-pass gradient update                                */
/* ------------------------------------------------------------------ */

static void hb_update(const PPhotoModel *m, double f_std,
                      int nrows, int ncols,
                      const double *I,         /* [nrows*ncols] reflectance  */
                      double *pg, double *qg,  /* [nrows*ncols] gradient in  */
                      double *pn, double *qn,  /* [nrows*ncols] gradient out */
                      double sx, double sy, double sz, double g_deg,
                      double albedo, double lambda)
{
    const double dp = 1e-3;   /* numerical Jacobian step */
    const double lam2 = lambda * lambda;

#pragma omp parallel for schedule(static)
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            /* 4-neighbour averages of (p,q), clamped at boundary */
            int rp = r < nrows - 1 ? r + 1 : r;
            int rm = r > 0         ? r - 1 : r;
            int cp = c < ncols - 1 ? c + 1 : c;
            int cm = c > 0         ? c - 1 : c;

            double p_bar = 0.25 * (pg[r  * ncols + cm] + pg[r  * ncols + cp] +
                                   pg[rm * ncols + c ] + pg[rp * ncols + c ]);
            double q_bar = 0.25 * (qg[r  * ncols + cm] + qg[r  * ncols + cp] +
                                   qg[rm * ncols + c ] + qg[rp * ncols + c ]);

            /* Skip masked / NULL pixels */
            double I_obs = I[r * ncols + c];
            if (Rast_is_d_null_value(&I_obs)) {
                pn[r * ncols + c] = p_bar;
                qn[r * ncols + c] = q_bar;
                continue;
            }

            double R = I_obs / albedo;              /* target normalised brightness */
            double f  = eval_f(m, f_std, p_bar, q_bar, sx, sy, sz, g_deg);

            /* Numerical partial derivatives ∂f/∂p, ∂f/∂q */
            double fp = (eval_f(m, f_std, p_bar + dp, q_bar,      sx, sy, sz, g_deg) -
                         eval_f(m, f_std, p_bar - dp, q_bar,      sx, sy, sz, g_deg)) / (2.0 * dp);
            double fq = (eval_f(m, f_std, p_bar,      q_bar + dp, sx, sy, sz, g_deg) -
                         eval_f(m, f_std, p_bar,      q_bar - dp, sx, sy, sz, g_deg)) / (2.0 * dp);

            double residual = f - R;
            double denom    = fp * fp + fq * fq + lam2;

            pn[r * ncols + c] = p_bar - fp * residual / denom;
            qn[r * ncols + c] = q_bar - fq * residual / denom;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Two-pass gradient integration: row integration then column,        */
/* averaged, then shift to match seed-DEM mean                        */
/* ------------------------------------------------------------------ */

static void integrate_gradients(int nrows, int ncols,
                                 const double *pg, const double *qg,
                                 const double *z_seed,
                                 double *z_out, double res)
{
    double *z_x = G_malloc(nrows * ncols * sizeof(double));
    double *z_y = G_malloc(nrows * ncols * sizeof(double));

    /* Pass 1: integrate along rows (East) */
    for (int r = 0; r < nrows; r++) {
        z_x[r * ncols + 0] = z_seed[r * ncols + 0];
        for (int c = 1; c < ncols; c++)
            z_x[r * ncols + c] = z_x[r * ncols + c - 1]
                                  + pg[r * ncols + c] * res;
    }

    /* Pass 2: integrate along columns (North = increasing row index in GRASS) */
    for (int c = 0; c < ncols; c++) {
        z_y[0 * ncols + c] = z_seed[0 * ncols + c];
        for (int r = 1; r < nrows; r++)
            z_y[r * ncols + c] = z_y[(r - 1) * ncols + c]
                                  + qg[r * ncols + c] * res;
    }

    /* Average the two integration paths */
    double sum_out = 0.0, sum_seed = 0.0;
    long   n_valid = 0;
    for (int i = 0; i < nrows * ncols; i++) {
        z_out[i] = 0.5 * (z_x[i] + z_y[i]);
        if (!Rast_is_d_null_value(&z_seed[i])) {
            sum_out  += z_out[i];
            sum_seed += z_seed[i];
            n_valid++;
        }
    }

    /* Shift mean of output to match mean of seed DEM */
    if (n_valid > 0) {
        double shift = (sum_seed - sum_out) / (double)n_valid;
        for (int i = 0; i < nrows * ncols; i++)
            z_out[i] += shift;
    }

    G_free(z_x);
    G_free(z_y);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    struct GModule *module;

    /* Input/output options */
    struct Option *opt_input, *opt_output, *opt_seed, *opt_albedo;
    struct Option *opt_sunaz, *opt_sunelev;

    /* Photometric model */
    struct Option *opt_model;
    struct Option *opt_k, *opt_l, *opt_wh, *opt_hh, *opt_b0;
    struct Option *opt_hg1, *opt_hg2, *opt_theta;
    struct Option *opt_bh, *opt_ch;

    /* Algorithm tuning */
    struct Option *opt_niter, *opt_lambda;

    struct History history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Topography"));
    G_add_keyword(_("photoclinometry"));
    G_add_keyword(_("shape-from-shading"));
    G_add_keyword(_("DEM"));
    module->label = _("Photoclinometry: shape-from-shading DEM refinement.");
    module->description =
        _("Refines a seed DEM using a single calibrated reflectance image "
          "and a planetary photometric model (Hapke, Minnaert, LunarLambert, "
          "etc.). Implements the Horn-Brooks (1986) iterative gradient-domain "
          "shape-from-shading algorithm with numerical Jacobian evaluation. "
          "Phase angle is computed assuming nadir viewing geometry. "
          "Input reflectance must be calibrated (I/F or equivalent).");

    /* ---- Input / output ---- */
    opt_input = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->label = _("Calibrated reflectance raster (I/F)");

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->label = _("Output refined DEM raster");

    opt_seed = G_define_standard_option(G_OPT_R_INPUT);
    opt_seed->key      = "seed";
    opt_seed->required = NO;
    opt_seed->label    = _("Seed DEM raster (optional; flat if omitted)");
    opt_seed->description =
        _("Starting elevation field. Only low-frequency shape is needed; "
          "a coarse photometric or MOLA/LOLA DEM is ideal. "
          "If omitted, a flat (zero) surface is used.");

    opt_albedo = G_define_option();
    opt_albedo->key         = "albedo";
    opt_albedo->type        = TYPE_DOUBLE;
    opt_albedo->required    = YES;
    opt_albedo->label       = _("Surface normal albedo (dimensionless)");
    opt_albedo->description =
        _("Apparent normal reflectance of the surface. For the Hapke model "
          "this is approximately wh*0.5*(1+b0); for Minnaert it is the "
          "calibration-geometry I/F. Typical values: dark terrain 0.02-0.10, "
          "bright highland 0.15-0.35.");

    /* ---- Illumination ---- */
    opt_sunaz = G_define_option();
    opt_sunaz->key         = "sunaz";
    opt_sunaz->type        = TYPE_DOUBLE;
    opt_sunaz->required    = YES;
    opt_sunaz->label       = _("Solar azimuth [degrees, from North clockwise]");
    opt_sunaz->description = _("0=North, 90=East, 180=South, 270=West.");

    opt_sunelev = G_define_option();
    opt_sunelev->key         = "sunelev";
    opt_sunelev->type        = TYPE_DOUBLE;
    opt_sunelev->required    = YES;
    opt_sunelev->label       = _("Solar elevation [degrees above horizon]");
    opt_sunelev->description = _("Must be > 0 (sun above horizon).");

    /* ---- Photometric model ---- */
    opt_model = G_define_option();
    opt_model->key         = "model";
    opt_model->type        = TYPE_STRING;
    opt_model->required    = NO;
    opt_model->options     = "lambert,lommelseeliger,lunarlambert,minnaert,"
                             "hapkehen,hapkeleg,lunarlambertmcewen";
    opt_model->answer      = "minnaert";
    opt_model->description = _("Photometric model");

    opt_k = G_define_option();
    opt_k->key         = "k";
    opt_k->type        = TYPE_DOUBLE;
    opt_k->required    = NO;
    opt_k->answer      = "0.5";
    opt_k->label       = _("Minnaert K exponent");
    opt_k->description = _("K=1: Lambertian; K=0.5: lunar-like. Used only with model=minnaert.");

    opt_l = G_define_option();
    opt_l->key         = "l";
    opt_l->type        = TYPE_DOUBLE;
    opt_l->required    = NO;
    opt_l->answer      = "1.0";
    opt_l->label       = _("LunarLambert L mixing weight");
    opt_l->description = _("L=0: Lambertian; L=1: Lommel-Seeliger. Used with model=lunarlambert.");

    opt_wh = G_define_option();
    opt_wh->key         = "wh";
    opt_wh->type        = TYPE_DOUBLE;
    opt_wh->required    = NO;
    opt_wh->answer      = "0.5";
    opt_wh->description = _("Hapke single-scattering albedo omega [0,1]. Used with Hapke models.");

    opt_hh = G_define_option();
    opt_hh->key         = "hh";
    opt_hh->type        = TYPE_DOUBLE;
    opt_hh->required    = NO;
    opt_hh->answer      = "0.0";
    opt_hh->description = _("Hapke opposition surge width h. Used with Hapke models.");

    opt_b0 = G_define_option();
    opt_b0->key         = "b0";
    opt_b0->type        = TYPE_DOUBLE;
    opt_b0->required    = NO;
    opt_b0->answer      = "0.0";
    opt_b0->description = _("Hapke opposition surge amplitude B0. Used with Hapke models.");

    opt_hg1 = G_define_option();
    opt_hg1->key         = "hg1";
    opt_hg1->type        = TYPE_DOUBLE;
    opt_hg1->required    = NO;
    opt_hg1->answer      = "0.0";
    opt_hg1->description = _("HapkeHen 1st HG asymmetry coefficient. Used with model=hapkehen.");

    opt_hg2 = G_define_option();
    opt_hg2->key         = "hg2";
    opt_hg2->type        = TYPE_DOUBLE;
    opt_hg2->required    = NO;
    opt_hg2->answer      = "0.0";
    opt_hg2->description = _("HapkeHen 2nd component weight. Used with model=hapkehen.");

    opt_bh = G_define_option();
    opt_bh->key         = "bh";
    opt_bh->type        = TYPE_DOUBLE;
    opt_bh->required    = NO;
    opt_bh->answer      = "0.0";
    opt_bh->description = _("HapkeLeg Legendre b1 coefficient. Used with model=hapkeleg.");

    opt_ch = G_define_option();
    opt_ch->key         = "ch";
    opt_ch->type        = TYPE_DOUBLE;
    opt_ch->required    = NO;
    opt_ch->answer      = "0.0";
    opt_ch->description = _("HapkeLeg Legendre b2 coefficient. Used with model=hapkeleg.");

    opt_theta = G_define_option();
    opt_theta->key         = "theta";
    opt_theta->type        = TYPE_DOUBLE;
    opt_theta->required    = NO;
    opt_theta->answer      = "0.0";
    opt_theta->description = _("Hapke macroscopic roughness angle Theta [degrees]. Used with Hapke models.");

    /* ---- Algorithm parameters ---- */
    opt_niter = G_define_option();
    opt_niter->key         = "niter";
    opt_niter->type        = TYPE_INTEGER;
    opt_niter->required    = NO;
    opt_niter->answer      = "50";
    opt_niter->label       = _("Number of Horn-Brooks iterations");
    opt_niter->description = _("More iterations refine high-frequency topography further. "
                                "50-100 is sufficient for most applications.");

    opt_lambda = G_define_option();
    opt_lambda->key         = "lambda";
    opt_lambda->type        = TYPE_DOUBLE;
    opt_lambda->required    = NO;
    opt_lambda->answer      = "0.1";
    opt_lambda->label       = _("Horn regularization weight lambda");
    opt_lambda->description = _("Larger values suppress noise but reduce topographic amplitude. "
                                 "Typical range: 0.01 (steep terrain) to 0.5 (noisy data).");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* ---- Validate inputs ---- */
    double albedo  = atof(opt_albedo->answer);
    double sunaz   = atof(opt_sunaz->answer);
    double sunelev = atof(opt_sunelev->answer);
    int    niter   = atoi(opt_niter->answer);
    double lambda  = atof(opt_lambda->answer);

    if (albedo <= 0.0 || albedo > 1.0)
        G_fatal_error(_("albedo must be in (0, 1]."));
    if (sunelev <= 0.0 || sunelev >= 90.0)
        G_fatal_error(_("sunelev must be in (0, 90)."));
    if (niter < 1 || niter > 10000)
        G_fatal_error(_("niter must be in [1, 10000]."));
    if (lambda <= 0.0)
        G_fatal_error(_("lambda must be > 0."));

    /* ---- Build photometric model ---- */
    PPhotoModelType mtype;
    PPhmParams      mparams;
    const char     *mname = opt_model->answer;

    if      (strcmp(mname, "lambert") == 0) {
        mtype   = P_PHOTOMODEL_LAMBERT;
        mparams = (PPhmParams)P_PHM_DEFAULTS_LAMBERT;
    }
    else if (strcmp(mname, "lommelseeliger") == 0) {
        mtype   = P_PHOTOMODEL_LOMMELSEELIGER;
        mparams = (PPhmParams)P_PHM_DEFAULTS_LOMMELSEELIGER;
    }
    else if (strcmp(mname, "lunarlambert") == 0) {
        mtype              = P_PHOTOMODEL_LUNARLAMBERT;
        mparams            = (PPhmParams)P_PHM_DEFAULTS_LUNARLAMBERT;
        mparams.lunarlambert.L = atof(opt_l->answer);
    }
    else if (strcmp(mname, "minnaert") == 0) {
        mtype              = P_PHOTOMODEL_MINNAERT;
        mparams            = (PPhmParams)P_PHM_DEFAULTS_MINNAERT;
        mparams.minnaert.K = atof(opt_k->answer);
    }
    else if (strcmp(mname, "hapkehen") == 0) {
        mtype = P_PHOTOMODEL_HAPKE_HEN;
        mparams = (PPhmParams)P_PHM_DEFAULTS_HAPKE_HEN;
        mparams.hapke_hen.wh    = atof(opt_wh->answer);
        mparams.hapke_hen.hh    = atof(opt_hh->answer);
        mparams.hapke_hen.b0    = atof(opt_b0->answer);
        mparams.hapke_hen.hg1   = atof(opt_hg1->answer);
        mparams.hapke_hen.hg2   = atof(opt_hg2->answer);
        mparams.hapke_hen.theta = atof(opt_theta->answer);
    }
    else if (strcmp(mname, "hapkeleg") == 0) {
        mtype = P_PHOTOMODEL_HAPKE_LEG;
        mparams = (PPhmParams)P_PHM_DEFAULTS_HAPKE_LEG;
        mparams.hapke_leg.wh    = atof(opt_wh->answer);
        mparams.hapke_leg.hh    = atof(opt_hh->answer);
        mparams.hapke_leg.b0    = atof(opt_b0->answer);
        mparams.hapke_leg.bh    = atof(opt_bh->answer);
        mparams.hapke_leg.ch    = atof(opt_ch->answer);
        mparams.hapke_leg.theta = atof(opt_theta->answer);
    }
    else { /* lunarlambertmcewen */
        mtype   = P_PHOTOMODEL_LUNARLAMBERT_MCEWEN;
        mparams = (PPhmParams)P_PHM_DEFAULTS_LUNARLAMBERT_MCEWEN;
    }

    PPhotoModel *model = p_photomodel_create(mtype, &mparams);
    if (!model)
        G_fatal_error(_("Failed to create photometric model."));

    double f_std = p_photomodel_standard(model);
    if (f_std <= 0.0)
        G_fatal_error(_("Photometric model returned non-positive standard value."));

    /* ---- Sun direction vector in (E, N, Up) ---- */
    double az_rad  = sunaz  * M_PI / 180.0;
    double el_rad  = sunelev * M_PI / 180.0;
    double sx = sin(az_rad) * cos(el_rad);   /* East  */
    double sy = cos(az_rad) * cos(el_rad);   /* North */
    double sz = sin(el_rad);                 /* Up    */

    /* Phase angle (nadir viewing): g = arccos(sz) */
    double g_deg = acos(sz) * (180.0 / M_PI);

    G_message(_("Photometric model: %s  f_std=%.6f  g=%.2f°"),
              p_photomodel_name(model), f_std, g_deg);

    /* ---- Raster region ---- */
    struct Cell_head region;
    G_get_window(&region);
    int nrows = region.rows;
    int ncols = region.cols;
    double res = (region.ew_res + region.ns_res) * 0.5;  /* mean pixel size */

    /* ---- Read input reflectance ---- */
    int fd_input = Rast_open_old(opt_input->answer, "");
    double *I = G_malloc(nrows * ncols * sizeof(double));

    DCELL *rowbuf = G_malloc(ncols * sizeof(DCELL));
    for (int r = 0; r < nrows; r++) {
        Rast_get_d_row(fd_input, rowbuf, r);
        for (int c = 0; c < ncols; c++)
            I[r * ncols + c] = (double)rowbuf[c];
    }
    Rast_close(fd_input);

    /* ---- Read or build seed DEM ---- */
    double *z_seed = G_calloc(nrows * ncols, sizeof(double));
    if (opt_seed->answer) {
        int fd_seed = Rast_open_old(opt_seed->answer, "");
        for (int r = 0; r < nrows; r++) {
            Rast_get_d_row(fd_seed, rowbuf, r);
            for (int c = 0; c < ncols; c++)
                z_seed[r * ncols + c] = (double)rowbuf[c];
        }
        Rast_close(fd_seed);
        G_message(_("Using seed DEM: %s"), opt_seed->answer);
    }
    else {
        G_message(_("No seed DEM — starting from flat surface."));
    }
    G_free(rowbuf);

    /* ---- Initialise gradient field (p,q) from seed DEM ---- */
    double *pg = G_malloc(nrows * ncols * sizeof(double));
    double *qg = G_malloc(nrows * ncols * sizeof(double));

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            int cp = c < ncols - 1 ? c + 1 : c;
            int cm = c > 0         ? c - 1 : c;
            int rp = r < nrows - 1 ? r + 1 : r;
            int rm = r > 0         ? r - 1 : r;
            int dc = cp - cm;   /* 2 or 1 at boundary */
            int dr = rp - rm;

            pg[r * ncols + c] = dc > 0
                ? (z_seed[r * ncols + cp] - z_seed[r * ncols + cm]) / (dc * res)
                : 0.0;
            qg[r * ncols + c] = dr > 0
                ? (z_seed[rp * ncols + c] - z_seed[rm * ncols + c]) / (dr * res)
                : 0.0;
        }
    }

    /* ---- Iterative Horn-Brooks gradient update ---- */
    double *pn = G_malloc(nrows * ncols * sizeof(double));
    double *qn = G_malloc(nrows * ncols * sizeof(double));

    for (int it = 0; it < niter; it++) {
        if ((it + 1) % 10 == 0 || it == 0)
            G_percent(it, niter, 2);

        hb_update(model, f_std, nrows, ncols,
                  I, pg, qg, pn, qn,
                  sx, sy, sz, g_deg, albedo, lambda);

        /* swap buffers */
        double *tmp;
        tmp = pg; pg = pn; pn = tmp;
        tmp = qg; qg = qn; qn = tmp;
    }
    G_percent(niter, niter, 2);

    G_free(pn);
    G_free(qn);

    /* ---- Integrate gradient field to get height ---- */
    double *z_out = G_malloc(nrows * ncols * sizeof(double));
    integrate_gradients(nrows, ncols, pg, qg, z_seed, z_out, res);

    G_free(pg);
    G_free(qg);
    G_free(z_seed);
    G_free(I);

    /* ---- Write output DEM ---- */
    int fd_output = Rast_open_new(opt_output->answer, DCELL_TYPE);
    DCELL *outbuf = G_malloc(ncols * sizeof(DCELL));

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++)
            outbuf[c] = (DCELL)z_out[r * ncols + c];
        Rast_put_d_row(fd_output, outbuf);
    }
    Rast_close(fd_output);
    G_free(outbuf);
    G_free(z_out);

    p_photomodel_free(model);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);

    G_message(_("Output DEM written to <%s>."), opt_output->answer);

    return EXIT_SUCCESS;
}
