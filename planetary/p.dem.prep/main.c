#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.dem.prep
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Prepare a planetary DEM for photometric processing.
 *               Fills voids (NULL pixels), re-scales from radius-from-centre
 *               to topographic height above the reference ellipsoid, and
 *               clips to the valid data range.
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
#include "../../libs/p_shapemodel/p_shapemodel.h"

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_a, *opt_c;
    struct Option  *opt_fill, *opt_minval, *opt_maxval;
    struct Flag    *flag_height, *flag_fill;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Terrain Analysis"));
    G_add_keyword(_("DEM"));
    G_add_keyword(_("terrain"));
    module->label       = _("Prepare a planetary DEM for photometric and terrain analysis.");
    module->description = _("Processes a planetary elevation or radius map: "
                             "optionally fills NULL voids using the reference ellipsoid "
                             "radius, converts absolute radius-from-centre to "
                             "topographic height above the ellipsoid, and clips "
                             "extreme outliers.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_a = G_define_option(); opt_a->key="a_radius"; opt_a->type=TYPE_DOUBLE;
    opt_a->required=NO; opt_a->answer="3396.19"; opt_a->description=_("Ellipsoid equatorial radius [km]");
    opt_c = G_define_option(); opt_c->key="c_radius"; opt_c->type=TYPE_DOUBLE;
    opt_c->required=NO; opt_c->answer="3376.20"; opt_c->description=_("Ellipsoid polar radius [km]");

    opt_fill = G_define_option(); opt_fill->key="fill_value"; opt_fill->type=TYPE_STRING;
    opt_fill->required=NO; opt_fill->answer="ellipsoid";
    opt_fill->options="ellipsoid,mean,null";
    opt_fill->description=_("Value to fill NULL voids with");

    opt_minval = G_define_option(); opt_minval->key="minval"; opt_minval->type=TYPE_DOUBLE;
    opt_minval->required=NO; opt_minval->description=_("Clip values below this to NULL (km)");
    opt_maxval = G_define_option(); opt_maxval->key="maxval"; opt_maxval->type=TYPE_DOUBLE;
    opt_maxval->required=NO; opt_maxval->description=_("Clip values above this to NULL (km)");

    flag_height = G_define_flag(); flag_height->key='h';
    flag_height->description=_("Convert radius-from-centre to height above ellipsoid");
    flag_fill = G_define_flag(); flag_fill->key='f';
    flag_fill->description=_("Fill NULL voids before other processing");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    double a=atof(opt_a->answer), c=atof(opt_c->answer);
    PShapeModel *shape = p_shape_ellipsoid(a, a, c); /* assume b==a */

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    DCELL *buf_in  = Rast_allocate_d_buf();
    DCELL *buf_out = Rast_allocate_d_buf();

    /* Compute mean for fill option */
    double mean_val = a;
    if (opt_fill->answer && strcmp(opt_fill->answer,"mean")==0) {
        double sum=0; long cnt=0;
        for (int r=0; r<nrows; r++) {
            Rast_get_d_row(fd_in, buf_in, r);
            for (int c2=0; c2<ncols; c2++)
                if (!Rast_is_d_null_value(&buf_in[c2])) { sum+=buf_in[c2]; cnt++; }
        }
        if (cnt>0) mean_val = sum/cnt;
        /* Rewind by reopening */
        Rast_close(fd_in);
        fd_in = Rast_open_old(opt_input->answer, "");
    }

    int do_clip_min = (opt_minval->answer != NULL);
    int do_clip_max = (opt_maxval->answer != NULL);
    double clip_min = do_clip_min ? atof(opt_minval->answer) : 0;
    double clip_max = do_clip_max ? atof(opt_maxval->answer) : 0;

    long filled=0, clipped=0;

    for (int row=0; row<nrows; row++) {
        G_percent(row, nrows, 2);
        Rast_get_d_row(fd_in, buf_in, row);
        for (int col=0; col<ncols; col++) {
            double val;
            if (Rast_is_d_null_value(&buf_in[col])) {
                if (!flag_fill->answer) {
                    Rast_set_d_null_value(&buf_out[col], 1);
                    continue;
                }
                /* Fill */
                double east  = reg.west  + (col+0.5)*reg.ew_res;
                double north = reg.north - (row+0.5)*reg.ns_res;
                if (strcmp(opt_fill->answer,"ellipsoid")==0)
                    val = p_shape_local_radius_km(shape, north, east);
                else if (strcmp(opt_fill->answer,"mean")==0)
                    val = mean_val;
                else {
                    Rast_set_d_null_value(&buf_out[col], 1); continue;
                }
                filled++;
            } else {
                val = buf_in[col];
            }
            /* Clip */
            if ((do_clip_min && val < clip_min) || (do_clip_max && val > clip_max)) {
                Rast_set_d_null_value(&buf_out[col], 1); clipped++; continue;
            }
            /* Convert radius to height above ellipsoid */
            if (flag_height->answer) {
                double east  = reg.west  + (col+0.5)*reg.ew_res;
                double north = reg.north - (row+0.5)*reg.ns_res;
                double r_ell = p_shape_local_radius_km(shape, north, east);
                val = val - r_ell;
            }
            buf_out[col] = (DCELL)val;
        }
        Rast_put_d_row(fd_out, buf_out);
    }
    G_percent(1,1,2);
    Rast_close(fd_in); Rast_close(fd_out);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);
    G_free(buf_in); G_free(buf_out);
    p_shape_free(shape);
    G_message(_("p.dem.prep: %ld voids filled, %ld pixels clipped."), filled, clipped);
    return EXIT_SUCCESS;
}
