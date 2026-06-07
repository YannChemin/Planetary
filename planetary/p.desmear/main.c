/****************************************************************************
 *
 * MODULE:       p.desmear
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Correct readout smear in framing cameras (Viking, Galileo SSI,
 *               Voyager).  Smear is the residual exposure accumulated during
 *               frame-transfer readout at exposure_time/readout_time fraction
 *               of the scene mean per row.
 *
 *               smear[row] = (exposure_time / readout_time) * row_mean(all rows)
 *               corrected  = input - smear
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
    struct Option  *opt_input, *opt_output, *opt_exp, *opt_rdout;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Noise & Calibration"));
    G_add_keyword(_("smear"));
    G_add_keyword(_("framing camera"));
    G_add_keyword(_("calibration"));
    module->label = _("Correct frame-transfer readout smear in planetary framing cameras.");
    module->description = _("Removes the smear signal accumulated during frame-transfer "
                             "readout (common in Viking, Galileo SSI, Voyager images). "
                             "The smear contribution per row is proportional to the image "
                             "mean and the ratio of exposure time to readout time.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_exp = G_define_option(); opt_exp->key="exposure_time";
    opt_exp->type=TYPE_DOUBLE; opt_exp->required=YES;
    opt_exp->description=_("Image exposure time [ms]");
    opt_rdout = G_define_option(); opt_rdout->key="readout_time";
    opt_rdout->type=TYPE_DOUBLE; opt_rdout->required=YES;
    opt_rdout->description=_("Frame readout time [ms]");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    double exp_t  = atof(opt_exp->answer);
    double rdt    = atof(opt_rdout->answer);
    if (rdt <= 0.0) G_fatal_error(_("readout_time must be > 0"));
    double frac = exp_t / rdt;
    G_message(_("Smear fraction: exposure/readout = %.4f"), frac);

    int fd_in = Rast_open_old(opt_input->answer, "");
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    /* Compute global image mean (needed for the smear signal per row) */
    DCELL *buf = Rast_allocate_d_buf();
    double sum=0; long cnt=0;
    for(int r=0;r<nrows;r++){
        Rast_get_d_row(fd_in,buf,r);
        for(int c=0;c<ncols;c++)
            if(!Rast_is_d_null_value(&buf[c])){sum+=buf[c];cnt++;}
    }
    double img_mean = (cnt>0) ? sum/cnt : 0.0;
    G_message(_("Image mean DN: %.4f"), img_mean);

    /* Rewind and apply correction */
    Rast_close(fd_in);
    fd_in = Rast_open_old(opt_input->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    DCELL *buf_out = Rast_allocate_d_buf();

    for(int r=0;r<nrows;r++){
        G_percent(r,nrows,2);
        Rast_get_d_row(fd_in,buf,r);
        double smear = frac * img_mean * r / nrows;
        for(int c=0;c<ncols;c++){
            if(Rast_is_d_null_value(&buf[c]))
                Rast_set_d_null_value(&buf_out[c],1);
            else
                buf_out[c]=(DCELL)(buf[c]-smear);
        }
        Rast_put_d_row(fd_out,buf_out);
    }
    G_percent(1,1,2);
    Rast_close(fd_in); Rast_close(fd_out);
    G_free(buf); G_free(buf_out);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);
    G_message(_("Smear correction applied: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
