/****************************************************************************
 *
 * MODULE:       p.photrim
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Trim a planetary raster by photometric angle thresholds.
 *               Pixels where any angle exceeds the user threshold become NULL.
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

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_incidence, *opt_emission, *opt_phase;
    struct Option  *opt_max_inc, *opt_max_emi, *opt_max_pha;
    struct Option  *opt_min_inc, *opt_min_emi;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Photometric Analysis"));
    G_add_keyword(_("photometry"));
    G_add_keyword(_("mask"));
    module->label       = _("Trim a planetary raster by photometric angle thresholds.");
    module->description = _("Sets pixels to NULL where the incidence, emission, or phase "
                             "angle exceeds the specified maximum (or falls below minimum). "
                             "Angle backplanes are those produced by p.phocube.");

    opt_input      = G_define_standard_option(G_OPT_R_INPUT);
    opt_output     = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_incidence  = G_define_standard_option(G_OPT_R_INPUT);
    opt_incidence->key = "incidence";
    opt_incidence->required = NO;
    opt_incidence->description = _("Incidence angle backplane [deg] (optional)");
    opt_emission   = G_define_standard_option(G_OPT_R_INPUT);
    opt_emission->key = "emission";
    opt_emission->required = NO;
    opt_emission->description = _("Emission angle backplane [deg] (optional)");
    opt_phase      = G_define_standard_option(G_OPT_R_INPUT);
    opt_phase->key = "phase";
    opt_phase->required = NO;
    opt_phase->description = _("Phase angle backplane [deg] (optional)");

    opt_max_inc = G_define_option(); opt_max_inc->key = "maxincidence";
    opt_max_inc->type = TYPE_DOUBLE; opt_max_inc->required = NO;
    opt_max_inc->answer = "85.0";
    opt_max_inc->description = _("Maximum allowed incidence angle [deg]");

    opt_max_emi = G_define_option(); opt_max_emi->key = "maxemission";
    opt_max_emi->type = TYPE_DOUBLE; opt_max_emi->required = NO;
    opt_max_emi->answer = "85.0";
    opt_max_emi->description = _("Maximum allowed emission angle [deg]");

    opt_max_pha = G_define_option(); opt_max_pha->key = "maxphase";
    opt_max_pha->type = TYPE_DOUBLE; opt_max_pha->required = NO;
    opt_max_pha->answer = "120.0";
    opt_max_pha->description = _("Maximum allowed phase angle [deg]");

    opt_min_inc = G_define_option(); opt_min_inc->key = "minincidence";
    opt_min_inc->type = TYPE_DOUBLE; opt_min_inc->required = NO;
    opt_min_inc->answer = "0.0";
    opt_min_inc->description = _("Minimum allowed incidence angle [deg]");

    opt_min_emi = G_define_option(); opt_min_emi->key = "minemission";
    opt_min_emi->type = TYPE_DOUBLE; opt_min_emi->required = NO;
    opt_min_emi->answer = "0.0";
    opt_min_emi->description = _("Minimum allowed emission angle [deg]");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    double max_inc = atof(opt_max_inc->answer);
    double max_emi = atof(opt_max_emi->answer);
    double max_pha = atof(opt_max_pha->answer);
    double min_inc = atof(opt_min_inc->answer);
    double min_emi = atof(opt_min_emi->answer);

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_inc = opt_incidence->answer ? Rast_open_old(opt_incidence->answer, "") : -1;
    int fd_emi = opt_emission->answer  ? Rast_open_old(opt_emission->answer, "")  : -1;
    int fd_pha = opt_phase->answer     ? Rast_open_old(opt_phase->answer, "")     : -1;
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);

    struct Cell_head region;
    G_get_window(&region);
    int nrows = region.rows, ncols = region.cols;

    DCELL *buf_in  = Rast_allocate_d_buf();
    DCELL *buf_inc = (fd_inc >= 0) ? Rast_allocate_d_buf() : NULL;
    DCELL *buf_emi = (fd_emi >= 0) ? Rast_allocate_d_buf() : NULL;
    DCELL *buf_pha = (fd_pha >= 0) ? Rast_allocate_d_buf() : NULL;
    DCELL *buf_out = Rast_allocate_d_buf();

    long trimmed = 0, total = 0;

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        Rast_get_d_row(fd_in, buf_in, row);
        if (fd_inc >= 0) Rast_get_d_row(fd_inc, buf_inc, row);
        if (fd_emi >= 0) Rast_get_d_row(fd_emi, buf_emi, row);
        if (fd_pha >= 0) Rast_get_d_row(fd_pha, buf_pha, row);

        for (int c = 0; c < ncols; c++) {
            total++;
            if (Rast_is_d_null_value(&buf_in[c])) {
                Rast_set_d_null_value(&buf_out[c], 1);
                continue;
            }
            int trim = 0;
            if (fd_inc >= 0 && !Rast_is_d_null_value(&buf_inc[c])) {
                if (buf_inc[c] > max_inc || buf_inc[c] < min_inc) trim = 1;
            }
            if (!trim && fd_emi >= 0 && !Rast_is_d_null_value(&buf_emi[c])) {
                if (buf_emi[c] > max_emi || buf_emi[c] < min_emi) trim = 1;
            }
            if (!trim && fd_pha >= 0 && !Rast_is_d_null_value(&buf_pha[c])) {
                if (buf_pha[c] > max_pha) trim = 1;
            }
            if (trim) {
                Rast_set_d_null_value(&buf_out[c], 1);
                trimmed++;
            } else {
                buf_out[c] = buf_in[c];
            }
        }
        Rast_put_d_row(fd_out, buf_out);
    }
    G_percent(1, 1, 2);

    Rast_close(fd_in);
    if (fd_inc >= 0) Rast_close(fd_inc);
    if (fd_emi >= 0) Rast_close(fd_emi);
    if (fd_pha >= 0) Rast_close(fd_pha);
    Rast_close(fd_out);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);

    G_free(buf_in); G_free(buf_out);
    if (buf_inc) G_free(buf_inc);
    if (buf_emi) G_free(buf_emi);
    if (buf_pha) G_free(buf_pha);

    G_message(_("p.photrim: %ld of %ld pixels trimmed (%.1f%%)."),
               trimmed, total, 100.0 * trimmed / (total > 0 ? total : 1));
    return EXIT_SUCCESS;
}
