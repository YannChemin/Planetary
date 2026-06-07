#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.atcorr.hapke
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Apply Hapke-model atmospheric correction to a planetary raster.
 *
 *               Computes atmospheric terms pstd, trans, trans0, sbar using one
 *               of the four ISIS3-derived scattering models (Isotropic1/2,
 *               Anisotropic1/2) and applies the full correction formula:
 *
 *               P = pstd + trans * rho * Ah * cos(inc) / (1 - rho*Ab*sbar)
 *                        + trans0 * rho * (Psurf - Ah * cos(inc))
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "../../libs/p_atmosmodel/p_atmosmodel.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_model;
    struct Option  *opt_incidence, *opt_emission, *opt_phase;
    struct Option  *opt_tau, *opt_wha, *opt_hnorm, *opt_bha;
    struct Option  *opt_rho, *opt_ah, *opt_ab;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Photometric Analysis"));
    G_add_keyword(_("atmospheric correction"));
    G_add_keyword(_("Hapke"));
    G_add_keyword(_("Mars"));
    module->label       = _("Apply Hapke-model atmospheric correction to a planetary raster.");
    module->description = _("Removes the effect of a thin planetary atmosphere (Mars, Titan) "
                             "using one of four ISIS3-derived atmospheric scattering models "
                             "(Isotropic1, Isotropic2, Anisotropic1, Anisotropic2). "
                             "Requires per-pixel geometry backplanes from p.phocube.");

    opt_input      = G_define_standard_option(G_OPT_R_INPUT);
    opt_output     = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_incidence  = G_define_standard_option(G_OPT_R_INPUT);
    opt_incidence->key = "incidence";
    opt_incidence->description = _("Incidence angle backplane [deg]");
    opt_emission   = G_define_standard_option(G_OPT_R_INPUT);
    opt_emission->key = "emission";
    opt_emission->description = _("Emission angle backplane [deg]");
    opt_phase      = G_define_standard_option(G_OPT_R_INPUT);
    opt_phase->key = "phase";
    opt_phase->description = _("Phase angle backplane [deg]");

    opt_model = G_define_option();
    opt_model->key = "model"; opt_model->type = TYPE_STRING;
    opt_model->required = NO; opt_model->answer = "isotropic1";
    opt_model->options  = "isotropic1,isotropic2,anisotropic1,anisotropic2";
    opt_model->description = _("Atmospheric scattering model");

    opt_tau = G_define_option(); opt_tau->key = "tau"; opt_tau->type = TYPE_DOUBLE;
    opt_tau->required = NO; opt_tau->answer = "0.28";
    opt_tau->description = _("Atmospheric optical depth (tau) — Mars dust: 0.28–1.5");

    opt_wha = G_define_option(); opt_wha->key = "wha"; opt_wha->type = TYPE_DOUBLE;
    opt_wha->required = NO; opt_wha->answer = "0.95";
    opt_wha->description = _("Atmospheric dust single-scatter albedo (wha)");

    opt_hnorm = G_define_option(); opt_hnorm->key = "hnorm"; opt_hnorm->type = TYPE_DOUBLE;
    opt_hnorm->required = NO; opt_hnorm->answer = "0.05";
    opt_hnorm->description = _("Atmospheric shell thickness / planet radius");

    opt_bha = G_define_option(); opt_bha->key = "bha"; opt_bha->type = TYPE_DOUBLE;
    opt_bha->required = NO; opt_bha->answer = "0.85";
    opt_bha->description = _("Anisotropy parameter bha (Anisotropic models only)");

    opt_rho = G_define_option(); opt_rho->key = "rho"; opt_rho->type = TYPE_DOUBLE;
    opt_rho->required = NO; opt_rho->answer = "1.0";
    opt_rho->description = _("Surface albedo / reference albedo ratio rho");

    opt_ah = G_define_option(); opt_ah->key = "ah"; opt_ah->type = TYPE_DOUBLE;
    opt_ah->required = NO; opt_ah->answer = "0.5";
    opt_ah->description = _("Directional hemispheric albedo Ah (from photometric model)");

    opt_ab = G_define_option(); opt_ab->key = "ab"; opt_ab->type = TYPE_DOUBLE;
    opt_ab->required = NO; opt_ab->answer = "0.3";
    opt_ab->description = _("Bi-hemispheric albedo Ab");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* Build atmospheric model */
    PAtmParams ap = P_ATM_DEFAULTS_ISOTROPIC1;
    ap.tau   = atof(opt_tau->answer);
    ap.wha   = atof(opt_wha->answer);
    ap.hnorm = atof(opt_hnorm->answer);
    ap.bha   = atof(opt_bha->answer);

    PAtmosModelType mtype = P_ATMOSMODEL_ISOTROPIC1;
    const char *mn = opt_model->answer;
    if (strcmp(mn,"isotropic2")==0)   mtype = P_ATMOSMODEL_ISOTROPIC2;
    else if (strcmp(mn,"anisotropic1")==0) mtype = P_ATMOSMODEL_ANISOTROPIC1;
    else if (strcmp(mn,"anisotropic2")==0) mtype = P_ATMOSMODEL_ANISOTROPIC2;

    PAtmosModel *atm = p_atmosmodel_create(mtype, &ap);
    if (!atm) G_fatal_error(_("Cannot create atmospheric model '%s'"), mn);

    double rho = atof(opt_rho->answer);
    double Ah  = atof(opt_ah->answer);
    double Ab  = atof(opt_ab->answer);

    G_message(_("Atmospheric model: %s  tau=%.3f  wha=%.3f"),
               p_atmosmodel_name(atm), ap.tau, ap.wha);

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_inc = Rast_open_old(opt_incidence->answer, "");
    int fd_emi = Rast_open_old(opt_emission->answer, "");
    int fd_pha = Rast_open_old(opt_phase->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);

    struct Cell_head region; G_get_window(&region);
    int nrows = region.rows, ncols = region.cols;

    DCELL *b_in  = Rast_allocate_d_buf();
    DCELL *b_inc = Rast_allocate_d_buf();
    DCELL *b_emi = Rast_allocate_d_buf();
    DCELL *b_pha = Rast_allocate_d_buf();
    DCELL *b_out = Rast_allocate_d_buf();

    double *in_d   = (double*)G_malloc((size_t)ncols*sizeof(double));
    double *psurf  = (double*)G_malloc((size_t)ncols*sizeof(double));
    double *inc_d  = (double*)G_malloc((size_t)ncols*sizeof(double));
    double *emi_d  = (double*)G_malloc((size_t)ncols*sizeof(double));
    double *pha_d  = (double*)G_malloc((size_t)ncols*sizeof(double));
    double *out_d  = (double*)G_malloc((size_t)ncols*sizeof(double));

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        Rast_get_d_row(fd_in,  b_in,  row);
        Rast_get_d_row(fd_inc, b_inc, row);
        Rast_get_d_row(fd_emi, b_emi, row);
        Rast_get_d_row(fd_pha, b_pha, row);

        for (int c = 0; c < ncols; c++) {
            in_d[c]  = Rast_is_d_null_value(&b_in[c])  ? NAN : b_in[c];
            psurf[c] = in_d[c]; /* Psurf = corrected input */
            inc_d[c] = Rast_is_d_null_value(&b_inc[c]) ? NAN : b_inc[c];
            emi_d[c] = Rast_is_d_null_value(&b_emi[c]) ? NAN : b_emi[c];
            pha_d[c] = Rast_is_d_null_value(&b_pha[c]) ? NAN : b_pha[c];
        }

        p_atmosmodel_apply_row(atm, ncols, in_d, psurf,
                                pha_d, inc_d, emi_d,
                                rho, Ah, Ab, out_d);

        for (int c = 0; c < ncols; c++) {
            if (out_d[c] != out_d[c])
                Rast_set_d_null_value(&b_out[c], 1);
            else
                b_out[c] = (DCELL)out_d[c];
        }
        Rast_put_d_row(fd_out, b_out);
    }
    G_percent(1, 1, 2);

    Rast_close(fd_in); Rast_close(fd_inc); Rast_close(fd_emi);
    Rast_close(fd_pha); Rast_close(fd_out);
    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);

    G_free(in_d); G_free(psurf); G_free(inc_d); G_free(emi_d);
    G_free(pha_d); G_free(out_d);
    G_free(b_in); G_free(b_inc); G_free(b_emi); G_free(b_pha); G_free(b_out);
    p_atmosmodel_free(atm);

    G_message(_("Atmospheric correction complete: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
