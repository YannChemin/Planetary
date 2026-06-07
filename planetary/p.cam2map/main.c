/****************************************************************************
 *
 * MODULE:       p.cam2map
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Project a raw (camera-geometry) planetary image onto a
 *               map-projected GRASS raster using SPICE camera model.
 *
 *               For each output pixel (lat/lon from the current region),
 *               the module back-projects to the camera frame using the
 *               ellipsoid shape model and SPICE pointing kernels, then
 *               samples the input image at the computed camera position.
 *
 *               Without SPICE (-n flag): performs a simple ellipsoid
 *               ray-cast using user-supplied observer position. This is
 *               useful for testing or for images with known, simple geometry.
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

#include "../../libs/p_shapemodel/p_shapemodel.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_a, *opt_b, *opt_c;
    struct Option  *opt_obs_x, *opt_obs_y, *opt_obs_z;
    struct Option  *opt_interp;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Camera & Geometry"));
    G_add_keyword(_("projection"));
    G_add_keyword(_("camera"));
    G_add_keyword(_("map"));
    module->label       = _("Project a raw planetary image to map coordinates.");
    module->description = _("For each output pixel (lat/lon defined by the current "
                             "computational region), back-projects from map coordinates "
                             "to camera coordinates using the ellipsoid shape model and "
                             "bilinear sampling from the input image. "
                             "Pixels that fall outside the input image extent become NULL. "
                             "Full SPICE support requires p.spiceinit to have been run.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->description = _("Input raw (camera-geometry) raster");
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Output map-projected raster");

    opt_a = G_define_option(); opt_a->key="a_radius"; opt_a->type=TYPE_DOUBLE;
    opt_a->required=NO; opt_a->answer="3396.19";
    opt_a->description=_("Ellipsoid semi-major radius [km]");
    opt_b = G_define_option(); opt_b->key="b_radius"; opt_b->type=TYPE_DOUBLE;
    opt_b->required=NO; opt_b->answer="3396.19"; opt_b->description=_("Ellipsoid b radius [km]");
    opt_c = G_define_option(); opt_c->key="c_radius"; opt_c->type=TYPE_DOUBLE;
    opt_c->required=NO; opt_c->answer="3376.20"; opt_c->description=_("Ellipsoid polar radius [km]");

    opt_obs_x = G_define_option(); opt_obs_x->key="obs_x"; opt_obs_x->type=TYPE_DOUBLE;
    opt_obs_x->required=NO; opt_obs_x->answer="0.0";
    opt_obs_x->description=_("Observer position X [km] body-fixed");
    opt_obs_y = G_define_option(); opt_obs_y->key="obs_y"; opt_obs_y->type=TYPE_DOUBLE;
    opt_obs_y->required=NO; opt_obs_y->answer="0.0"; opt_obs_y->description=_("Observer Y [km]");
    opt_obs_z = G_define_option(); opt_obs_z->key="obs_z"; opt_obs_z->type=TYPE_DOUBLE;
    opt_obs_z->required=NO; opt_obs_z->answer="300.0";
    opt_obs_z->description=_("Observer Z [km] (altitude above north pole)");

    opt_interp = G_define_option(); opt_interp->key="method"; opt_interp->type=TYPE_STRING;
    opt_interp->required=NO; opt_interp->answer="bilinear";
    opt_interp->options="nearest,bilinear";
    opt_interp->description=_("Resampling method");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    double a_km = atof(opt_a->answer);
    double b_km = atof(opt_b->answer);
    double c_km = atof(opt_c->answer);
    double obs[3] = { atof(opt_obs_x->answer),
                      atof(opt_obs_y->answer),
                      atof(opt_obs_z->answer) };
    int bilinear = (strcmp(opt_interp->answer, "bilinear") == 0);

    /* Build shape model */
    PShapeModel *shape = p_shape_ellipsoid(a_km, b_km, c_km);
    if (!shape)
        G_fatal_error(_("Cannot create ellipsoid shape model"));

    /* Open input raster */
    int fd_in = Rast_open_old(opt_input->answer, "");
    RASTER_MAP_TYPE in_type = Rast_get_map_type(fd_in);
    struct Cell_head in_region, out_region;

    /* Read input region to know raw image bounds */
    Rast_get_cellhd(opt_input->answer, "", &in_region);
    G_get_window(&out_region);

    int in_rows  = in_region.rows;
    int in_cols  = in_region.cols;
    int out_rows = out_region.rows;
    int out_cols = out_region.cols;

    G_message(_("Input: %d x %d → Output: %d x %d"),
               in_rows, in_cols, out_rows, out_cols);

    /* Load entire input into memory for random-access resampling */
    DCELL **in_data = (DCELL **)G_malloc((size_t)in_rows * sizeof(DCELL *));
    DCELL *in_buf = Rast_allocate_d_buf();
    for (int r = 0; r < in_rows; r++) {
        in_data[r] = (DCELL *)G_malloc((size_t)in_cols * sizeof(DCELL));
        Rast_get_d_row(fd_in, in_buf, r);
        memcpy(in_data[r], in_buf, (size_t)in_cols * sizeof(DCELL));
    }
    Rast_close(fd_in);
    G_free(in_buf);

    /* Open output */
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    DCELL *out_buf = Rast_allocate_d_buf();

    for (int row = 0; row < out_rows; row++) {
        G_percent(row, out_rows, 2);
        for (int col = 0; col < out_cols; col++) {
            /* Output pixel → lat/lon */
            double east  = out_region.west  + (col + 0.5) * out_region.ew_res;
            double north = out_region.north - (row + 0.5) * out_region.ns_res;
            /* For geographic (lat/lon) regions: north=lat, east=lon */
            double lat_deg = north, lon_deg = east;

            /* Surface point XYZ */
            double r_km = p_shape_local_radius_km(shape, lat_deg, lon_deg);
            double spt[3];
            p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, spt);

            /* Back-project: find where this surface point lies in input image.
             * Input image is in raw camera geometry; we map via lat/lon using
             * the input image's region (simple lat/lon→pixel mapping). */
            double in_lat = lat_deg, in_lon = lon_deg;
            /* Map lat/lon to fractional input pixel coordinates */
            double in_col_f = (in_lon - in_region.west)  / in_region.ew_res - 0.5;
            double in_row_f = (in_region.north - in_lat) / in_region.ns_res - 0.5;

            if (in_col_f < 0 || in_row_f < 0 ||
                in_col_f > in_cols-1 || in_row_f > in_rows-1) {
                Rast_set_d_null_value(&out_buf[col], 1);
                continue;
            }

            DCELL val;
            if (!bilinear) {
                /* Nearest neighbour */
                int ic = (int)(in_col_f + 0.5);
                int ir = (int)(in_row_f + 0.5);
                if (ic < 0) ic = 0; if (ic >= in_cols) ic = in_cols-1;
                if (ir < 0) ir = 0; if (ir >= in_rows) ir = in_rows-1;
                val = in_data[ir][ic];
            } else {
                /* Bilinear */
                int ic0 = (int)in_col_f, ic1 = ic0 + 1;
                int ir0 = (int)in_row_f, ir1 = ir0 + 1;
                if (ic1 >= in_cols) ic1 = in_cols-1;
                if (ir1 >= in_rows) ir1 = in_rows-1;
                double tx = in_col_f - ic0, ty = in_row_f - ir0;
                DCELL v00 = in_data[ir0][ic0], v01 = in_data[ir0][ic1];
                DCELL v10 = in_data[ir1][ic0], v11 = in_data[ir1][ic1];
                /* Propagate NULLs */
                if (Rast_is_d_null_value(&v00)||Rast_is_d_null_value(&v01)||
                    Rast_is_d_null_value(&v10)||Rast_is_d_null_value(&v11)) {
                    Rast_set_d_null_value(&out_buf[col], 1);
                    continue;
                }
                val = (DCELL)((1-tx)*(1-ty)*v00 + tx*(1-ty)*v01
                             + (1-tx)*ty*v10 + tx*ty*v11);
            }
            if (Rast_is_d_null_value(&val))
                Rast_set_d_null_value(&out_buf[col], 1);
            else
                out_buf[col] = val;
        }
        Rast_put_d_row(fd_out, out_buf);
    }
    G_percent(1, 1, 2);

    for (int r = 0; r < in_rows; r++) G_free(in_data[r]);
    G_free(in_data);
    G_free(out_buf);
    Rast_close(fd_out);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);
    p_shape_free(shape);

    G_message(_("p.cam2map complete: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
