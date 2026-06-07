/****************************************************************************
 *
 * MODULE:       p.specpix
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Classify pixels into ISIS3 special pixel categories
 *               (NULL, LRS, LIS, HIS, HRS) based on user-defined DN ranges.
 *
 *               Output is a categorical raster where:
 *                 0 = valid pixel
 *                 1 = NULL (missing data)
 *                 2 = LRS  (low representation saturation)
 *                 3 = LIS  (low instrument saturation)
 *                 4 = HIS  (high instrument saturation)
 *                 5 = HRS  (high representation saturation)
 *
 *               Functionally equivalent to ISIS3's specpix application
 *               but operates on GRASS raster maps.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_null_min, *opt_null_max;
    struct Option  *opt_lrs_min,  *opt_lrs_max;
    struct Option  *opt_lis_min,  *opt_lis_max;
    struct Option  *opt_his_min,  *opt_his_max;
    struct Option  *opt_hrs_min,  *opt_hrs_max;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("special pixels"));
    G_add_keyword(_("ISIS3"));
    G_add_keyword(_("spectral"));
    module->label = _("Classify pixels into ISIS3 special pixel categories.");
    module->description = _("Maps pixel DN values into ISIS3/PDS special pixel classes: "
                             "NULL (missing), LRS (low repr. sat.), LIS (low instr. sat.), "
                             "HIS (high instr. sat.), HRS (high repr. sat.). "
                             "Output: 0=valid, 1=NULL, 2=LRS, 3=LIS, 4=HIS, 5=HRS.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_null_min = G_define_option(); opt_null_min->key="null_min";
    opt_null_min->type=TYPE_DOUBLE; opt_null_min->required=NO;
    opt_null_min->answer="0"; opt_null_min->description=_("NULL minimum DN");
    opt_null_max = G_define_option(); opt_null_max->key="null_max";
    opt_null_max->type=TYPE_DOUBLE; opt_null_max->required=NO;
    opt_null_max->answer="0"; opt_null_max->description=_("NULL maximum DN");

    opt_lrs_min = G_define_option(); opt_lrs_min->key="lrs_min";
    opt_lrs_min->type=TYPE_DOUBLE; opt_lrs_min->required=NO;
    opt_lrs_min->answer="1"; opt_lrs_min->description=_("LRS minimum DN");
    opt_lrs_max = G_define_option(); opt_lrs_max->key="lrs_max";
    opt_lrs_max->type=TYPE_DOUBLE; opt_lrs_max->required=NO;
    opt_lrs_max->answer="1"; opt_lrs_max->description=_("LRS maximum DN");

    opt_lis_min = G_define_option(); opt_lis_min->key="lis_min";
    opt_lis_min->type=TYPE_DOUBLE; opt_lis_min->required=NO;
    opt_lis_min->answer="2"; opt_lis_min->description=_("LIS minimum DN");
    opt_lis_max = G_define_option(); opt_lis_max->key="lis_max";
    opt_lis_max->type=TYPE_DOUBLE; opt_lis_max->required=NO;
    opt_lis_max->answer="2"; opt_lis_max->description=_("LIS maximum DN");

    opt_his_min = G_define_option(); opt_his_min->key="his_min";
    opt_his_min->type=TYPE_DOUBLE; opt_his_min->required=NO;
    opt_his_min->answer="65534"; opt_his_min->description=_("HIS minimum DN");
    opt_his_max = G_define_option(); opt_his_max->key="his_max";
    opt_his_max->type=TYPE_DOUBLE; opt_his_max->required=NO;
    opt_his_max->answer="65534"; opt_his_max->description=_("HIS maximum DN");

    opt_hrs_min = G_define_option(); opt_hrs_min->key="hrs_min";
    opt_hrs_min->type=TYPE_DOUBLE; opt_hrs_min->required=NO;
    opt_hrs_min->answer="65535"; opt_hrs_min->description=_("HRS minimum DN");
    opt_hrs_max = G_define_option(); opt_hrs_max->key="hrs_max";
    opt_hrs_max->type=TYPE_DOUBLE; opt_hrs_max->required=NO;
    opt_hrs_max->answer="65535"; opt_hrs_max->description=_("HRS maximum DN");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    double null_min=atof(opt_null_min->answer), null_max=atof(opt_null_max->answer);
    double lrs_min =atof(opt_lrs_min->answer),  lrs_max =atof(opt_lrs_max->answer);
    double lis_min =atof(opt_lis_min->answer),  lis_max =atof(opt_lis_max->answer);
    double his_min =atof(opt_his_min->answer),  his_max =atof(opt_his_max->answer);
    double hrs_min =atof(opt_hrs_min->answer),  hrs_max =atof(opt_hrs_max->answer);

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, CELL_TYPE);
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;
    DCELL *buf_in  = Rast_allocate_d_buf();
    CELL  *buf_out = Rast_allocate_c_buf();
    long counts[6] = {0};

    for (int row=0; row<nrows; row++) {
        G_percent(row,nrows,2);
        Rast_get_d_row(fd_in, buf_in, row);
        for (int c=0; c<ncols; c++) {
            if (Rast_is_d_null_value(&buf_in[c])) { buf_out[c]=1; counts[1]++; continue; }
            double v = buf_in[c];
            if      (v>=null_min && v<=null_max) { buf_out[c]=1; counts[1]++; }
            else if (v>=lrs_min  && v<=lrs_max)  { buf_out[c]=2; counts[2]++; }
            else if (v>=lis_min  && v<=lis_max)  { buf_out[c]=3; counts[3]++; }
            else if (v>=his_min  && v<=his_max)  { buf_out[c]=4; counts[4]++; }
            else if (v>=hrs_min  && v<=hrs_max)  { buf_out[c]=5; counts[5]++; }
            else                                  { buf_out[c]=0; counts[0]++; }
        }
        Rast_put_c_row(fd_out, buf_out);
    }
    G_percent(1,1,2);
    Rast_close(fd_in); Rast_close(fd_out);
    G_free(buf_in); G_free(buf_out);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);
    G_message(_("Valid:%ld  NULL:%ld  LRS:%ld  LIS:%ld  HIS:%ld  HRS:%ld"),
               counts[0],counts[1],counts[2],counts[3],counts[4],counts[5]);
    return EXIT_SUCCESS;
}
