/****************************************************************************
 *
 * MODULE:       p.spectral.planet
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Apply spectral filtering operations to a multi-band planetary
 *               raster group: high-pass, low-pass, divisive filter, or band
 *               ratio using the p_spectra library.
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
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include "../../libs/p_spectra/p_spectra.h"

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_operation, *opt_window;
    struct Option  *opt_wl1, *opt_wl2, *opt_wcsv;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("spectral"));
    G_add_keyword(_("hyperspectral"));
    G_add_keyword(_("imagery"));
    module->label       = _("Spectral filtering for planetary multi-band rasters.");
    module->description = _("Applies spectral-domain operations (high-pass subtraction, "
                             "divisive filter, band ratio) to a group of single-band rasters "
                             "treated as a spectral cube. Band wavelengths are read from a "
                             "two-column CSV (wavelength,width). "
                             "Input maps must be named input.1, input.2, etc.");

    opt_input = G_define_option(); opt_input->key="input";
    opt_input->type=TYPE_STRING; opt_input->required=YES;
    opt_input->description=_("Base name of input band rasters (input.1, .2, ...)");
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description=_("Output raster (band ratio / high-pass band index result)");

    opt_operation = G_define_option(); opt_operation->key="operation";
    opt_operation->type=TYPE_STRING; opt_operation->required=YES;
    opt_operation->options="highpass,divfilter,bandratio,banddepth";
    opt_operation->description=_("Spectral operation to apply");

    opt_window = G_define_option(); opt_window->key="window";
    opt_window->type=TYPE_INTEGER; opt_window->required=NO;
    opt_window->answer="5";
    opt_window->description=_("Window width in bands (highpass/divfilter)");

    opt_wl1 = G_define_option(); opt_wl1->key="wavelength1";
    opt_wl1->type=TYPE_DOUBLE; opt_wl1->required=NO;
    opt_wl1->description=_("Primary wavelength [µm] for bandratio or banddepth center");
    opt_wl2 = G_define_option(); opt_wl2->key="wavelength2";
    opt_wl2->type=TYPE_DOUBLE; opt_wl2->required=NO;
    opt_wl2->description=_("Secondary wavelength [µm] for bandratio denominator or banddepth shoulder");

    opt_wcsv = G_define_option(); opt_wcsv->key="wavelengths";
    opt_wcsv->type=TYPE_STRING; opt_wcsv->required=NO;
    opt_wcsv->description=_("CSV file with wavelength,width per band (if not uniform)");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *inbase = opt_input->answer;
    const char *op     = opt_operation->answer;
    int window = atoi(opt_window->answer);

    /* Count input bands */
    int nbands = 0;
    char mapname[512];
    for (int b = 1; b <= 10000; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b);
        if (!G_find_raster(mapname, "")) break;
        nbands++;
    }
    if (nbands == 0) G_fatal_error(_("No bands found named '%s.1', '%s.2', …"), inbase, inbase);
    G_message(_("Found %d bands for '%s'"), nbands, inbase);

    /* Build spectral definition */
    PSpectraDef *sd;
    if (opt_wcsv->answer) {
        sd = p_spectra_def_read_csv(opt_wcsv->answer);
        if (!sd) G_fatal_error(_("Cannot read wavelength CSV '%s'"), opt_wcsv->answer);
    } else {
        /* Uniform 1-µm-spaced definition */
        double *wl = (double *)G_malloc((size_t)nbands * sizeof(double));
        double *wd = (double *)G_malloc((size_t)nbands * sizeof(double));
        for (int b = 0; b < nbands; b++) { wl[b] = b + 1.0; wd[b] = 1.0; }
        sd = p_spectra_def_create(nbands, wl, wd);
        G_free(wl); G_free(wd);
    }
    if (!sd) G_fatal_error(_("Cannot create spectral definition"));

    /* Open input band rasters */
    int *fd_in = (int *)G_malloc((size_t)nbands * sizeof(int));
    for (int b = 0; b < nbands; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b+1);
        fd_in[b] = Rast_open_old(mapname, "");
    }

    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    DCELL **bufs_in  = (DCELL **)G_malloc((size_t)nbands * sizeof(DCELL *));
    DCELL  *buf_out  = Rast_allocate_d_buf();
    for (int b = 0; b < nbands; b++) bufs_in[b] = Rast_allocate_d_buf();

    /* Per-row spectrum buffer [ncols * nbands] row-major */
    double *spectra = (double *)G_malloc((size_t)ncols * nbands * sizeof(double));
    double *out_d   = (double *)G_malloc((size_t)ncols * sizeof(double));
    double *out2d   = (double *)G_malloc((size_t)ncols * nbands * sizeof(double));

    double wl1 = opt_wl1->answer ? atof(opt_wl1->answer) : 1.0;
    double wl2 = opt_wl2->answer ? atof(opt_wl2->answer) : 2.0;

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        for (int b = 0; b < nbands; b++) {
            Rast_get_d_row(fd_in[b], bufs_in[b], row);
            for (int c = 0; c < ncols; c++)
                spectra[(size_t)c*nbands + b] =
                    Rast_is_d_null_value(&bufs_in[b][c]) ? NAN : bufs_in[b][c];
        }

        if (strcmp(op,"highpass")==0) {
            p_spectra_apply_row_highpass(ncols, nbands, spectra, window, out2d);
            /* Write first band of high-pass as output */
            for (int c=0; c<ncols; c++) {
                double v = out2d[(size_t)c*nbands];
                buf_out[c] = (DCELL)(v != v ? NAN : v);
                if (v != v) Rast_set_d_null_value(&buf_out[c],1);
            }
        } else if (strcmp(op,"divfilter")==0) {
            p_spectra_apply_row_divfilter(ncols, nbands, spectra, window, out2d);
            for (int c=0; c<ncols; c++) {
                double v = out2d[(size_t)c*nbands];
                if (v != v) Rast_set_d_null_value(&buf_out[c],1);
                else buf_out[c] = (DCELL)v;
            }
        } else if (strcmp(op,"bandratio")==0) {
            p_spectra_apply_row_band_ratio(sd, ncols, nbands, spectra, wl1, wl2, 0, out_d);
            for (int c=0; c<ncols; c++) {
                if (out_d[c]!=out_d[c]) Rast_set_d_null_value(&buf_out[c],1);
                else buf_out[c]=(DCELL)out_d[c];
            }
        } else { /* banddepth */
            p_spectra_apply_row_band_depth(sd, ncols, nbands, spectra,
                                            wl1, wl1-0.5, wl2, 0, out_d);
            for (int c=0; c<ncols; c++) {
                if (out_d[c]!=out_d[c]) Rast_set_d_null_value(&buf_out[c],1);
                else buf_out[c]=(DCELL)out_d[c];
            }
        }
        Rast_put_d_row(fd_out, buf_out);
    }
    G_percent(1,1,2);

    for (int b=0; b<nbands; b++) { Rast_close(fd_in[b]); G_free(bufs_in[b]); }
    G_free(fd_in); G_free(bufs_in); G_free(buf_out);
    G_free(spectra); G_free(out_d); G_free(out2d);
    Rast_close(fd_out);
    p_spectra_def_free(sd);

    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);
    G_message(_("p.spectral.planet: %s complete."), op);
    return EXIT_SUCCESS;
}
