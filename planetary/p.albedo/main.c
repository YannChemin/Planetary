#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.albedo
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute the geometric or Bond albedo of a planetary surface
 *               from photometrically corrected data and model parameters.
 *
 *               Geometric albedo: ratio of surface brightness at opposition
 *               (phase=0) to a Lambertian disk of the same size.
 *               Bond albedo: integrated over all phase angles using the
 *               photometric model's hemispheric integral.
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

#include "../../libs/p_photomodel/p_photomodel.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_model;
    struct Option  *opt_k, *opt_l, *opt_wh, *opt_hh, *opt_b0;
    struct Option  *opt_hg1, *opt_hg2, *opt_theta;
    struct Flag    *flag_bond;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Photometric Analysis"));
    G_add_keyword(_("albedo"));
    G_add_keyword(_("photometry"));
    module->label       = _("Compute planetary surface albedo.");
    module->description = _("Scales a photometrically corrected planetary raster to "
                             "albedo units using the specified photometric model. "
                             "The output is the ratio of the corrected reflectance to "
                             "the model's standard-condition value (geometric albedo). "
                             "Use -b for an approximation to Bond (spherical) albedo.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_model  = G_define_option();
    opt_model->key = "model"; opt_model->type = TYPE_STRING;
    opt_model->required = NO; opt_model->answer = "minnaert";
    opt_model->options  = "lambert,lommelseeliger,lunarlambert,minnaert,"
                           "hapkehen,hapkeleg,lunarlambertmcewen";
    opt_model->description = _("Photometric model (same as used in p.photomet)");

    opt_k = G_define_option(); opt_k->key = "k"; opt_k->type = TYPE_DOUBLE;
    opt_k->required = NO; opt_k->answer = "1.0";
    opt_k->description = _("Minnaert K exponent");

    opt_l = G_define_option(); opt_l->key = "l"; opt_l->type = TYPE_DOUBLE;
    opt_l->required = NO; opt_l->answer = "1.0";
    opt_l->description = _("LunarLambert L weight");

    opt_wh = G_define_option(); opt_wh->key = "wh"; opt_wh->type = TYPE_DOUBLE;
    opt_wh->required = NO; opt_wh->answer = "0.5";
    opt_wh->description = _("Hapke single-scatter albedo ω");

    opt_hh = G_define_option(); opt_hh->key = "hh"; opt_hh->type = TYPE_DOUBLE;
    opt_hh->required = NO; opt_hh->answer = "0.0"; opt_hh->description = _("Hapke hh");
    opt_b0 = G_define_option(); opt_b0->key = "b0"; opt_b0->type = TYPE_DOUBLE;
    opt_b0->required = NO; opt_b0->answer = "0.0"; opt_b0->description = _("Hapke B0");
    opt_hg1 = G_define_option(); opt_hg1->key = "hg1"; opt_hg1->type = TYPE_DOUBLE;
    opt_hg1->required = NO; opt_hg1->answer = "0.0"; opt_hg1->description = _("Hapke hg1");
    opt_hg2 = G_define_option(); opt_hg2->key = "hg2"; opt_hg2->type = TYPE_DOUBLE;
    opt_hg2->required = NO; opt_hg2->answer = "0.0"; opt_hg2->description = _("Hapke hg2");
    opt_theta = G_define_option(); opt_theta->key = "theta"; opt_theta->type = TYPE_DOUBLE;
    opt_theta->required = NO; opt_theta->answer = "0.0"; opt_theta->description = _("Hapke Θ");

    flag_bond = G_define_flag(); flag_bond->key = 'b';
    flag_bond->description = _("Approximate Bond (spherical) albedo via Lambert phase integral");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* Build model */
    const char *mn = opt_model->answer;
    PPhmParams p;
    PPhotoModelType mt = P_PHOTOMODEL_MINNAERT;
    p.minnaert.K = atof(opt_k->answer);

    if (strcmp(mn,"lambert")==0)          mt = P_PHOTOMODEL_LAMBERT;
    else if (strcmp(mn,"lommelseeliger")==0) mt = P_PHOTOMODEL_LOMMELSEELIGER;
    else if (strcmp(mn,"lunarlambert")==0) {
        p.lunarlambert.L = atof(opt_l->answer); mt = P_PHOTOMODEL_LUNARLAMBERT;
    } else if (strcmp(mn,"hapkehen")==0) {
        p.hapke_hen.wh=atof(opt_wh->answer); p.hapke_hen.hh=atof(opt_hh->answer);
        p.hapke_hen.b0=atof(opt_b0->answer); p.hapke_hen.hg1=atof(opt_hg1->answer);
        p.hapke_hen.hg2=atof(opt_hg2->answer); p.hapke_hen.theta=atof(opt_theta->answer);
        p.hapke_hen.zero_b0_std=1; mt = P_PHOTOMODEL_HAPKE_HEN;
    } else if (strcmp(mn,"hapkeleg")==0) {
        p.hapke_leg.wh=atof(opt_wh->answer); p.hapke_leg.hh=atof(opt_hh->answer);
        p.hapke_leg.b0=atof(opt_b0->answer); mt = P_PHOTOMODEL_HAPKE_LEG;
    } else if (strcmp(mn,"lunarlambertmcewen")==0)
        mt = P_PHOTOMODEL_LUNARLAMBERT_MCEWEN;

    PPhotoModel *model = p_photomodel_create(mt,
        (mt==P_PHOTOMODEL_LAMBERT||mt==P_PHOTOMODEL_LOMMELSEELIGER||
         mt==P_PHOTOMODEL_LUNARLAMBERT_MCEWEN) ? NULL : &p);
    if (!model) G_fatal_error(_("Cannot create model '%s'"), mn);

    double std_val = p_photomodel_standard(model);

    /* Bond albedo phase integral approximation: A_bond ≈ A_geom * q
     * where q = integral over phase of p(g)*sin(g)dg / 2.
     * For Lambert q=1.5, for LS q≈1; we use numerical estimate. */
    double phase_int = 1.0;
    if (flag_bond->answer) {
        /* Numerical integration of 2*integral_0^pi f(i=0,e=0,g)*sin(g)dg
         * (simple trapezoidal, 180 steps) */
        double sum = 0.0;
        int N = 180;
        for (int i = 0; i < N; i++) {
            double g_deg = (i + 0.5) * 180.0 / N;
            double g_rad = g_deg * M_PI / 180.0;
            double fv = p_photomodel_eval(model, g_deg, 0.0, 0.0);
            sum += fv * sin(g_rad) * (180.0 / N) * M_PI / 180.0;
        }
        phase_int = 2.0 * sum;
        G_message(_("Bond albedo phase integral q = %.4f"), phase_int);
    }

    /* Scale factor: multiply input (already photometrically corrected) by 1/std_val */
    double scale = phase_int / std_val;

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    struct Cell_head region; G_get_window(&region);
    int nrows = region.rows, ncols = region.cols;
    DCELL *buf_in = Rast_allocate_d_buf();
    DCELL *buf_out = Rast_allocate_d_buf();

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        Rast_get_d_row(fd_in, buf_in, row);
        for (int c = 0; c < ncols; c++) {
            if (Rast_is_d_null_value(&buf_in[c]))
                Rast_set_d_null_value(&buf_out[c], 1);
            else
                buf_out[c] = buf_in[c] * scale;
        }
        Rast_put_d_row(fd_out, buf_out);
    }
    G_percent(1, 1, 2);
    Rast_close(fd_in); Rast_close(fd_out);
    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);
    G_free(buf_in); G_free(buf_out);
    p_photomodel_free(model);
    G_message(_("Albedo map: %s  (scale=%.6g)"), opt_output->answer, scale);
    return EXIT_SUCCESS;
}
