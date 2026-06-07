/****************************************************************************
 *
 * MODULE:       p.rings.project
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Project a raster to ring-plane coordinates using the
 *               RingCylindrical projection (from p_projection_planet).
 *               Input: raster in ring_radius/ring_longitude map coordinates.
 *               Output: projected raster in ring-plane (x,y) coordinates
 *               centred at the specified ring radius and longitude.
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
#include "../../libs/p_projection_planet/p_projection_planet.h"

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_center_r, *opt_center_lon;
    struct Flag    *flag_inv, *flag_cw;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Ring Plane Analysis"));
    G_add_keyword(_("ring plane"));
    G_add_keyword(_("Saturn"));
    G_add_keyword(_("projection"));
    module->label = _("Project a raster to/from ring-plane cylindrical coordinates.");
    module->description = _("Applies or inverts the RingCylindrical projection for "
                             "planetary ring imaging (Saturn, Jupiter, Uranus). "
                             "Forward: ring_radius/ring_lon → x/y map. "
                             "Inverse (-i): x/y → ring_radius/ring_lon. "
                             "The east,north of the current GRASS region are interpreted "
                             "as ring_longitude,ring_radius respectively.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_center_r = G_define_option(); opt_center_r->key="center_radius";
    opt_center_r->type=TYPE_DOUBLE; opt_center_r->required=YES;
    opt_center_r->description=_("Ring radius at projection centre [km]");
    opt_center_lon = G_define_option(); opt_center_lon->key="center_lon";
    opt_center_lon->type=TYPE_DOUBLE; opt_center_lon->required=NO;
    opt_center_lon->answer="0.0"; opt_center_lon->description=_("Ring longitude at centre [deg]");
    flag_inv = G_define_flag(); flag_inv->key='i';
    flag_inv->description=_("Inverse projection: (x,y) → (radius, longitude)");
    flag_cw = G_define_flag(); flag_cw->key='c';
    flag_cw->description=_("Clockwise ring longitude direction");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    PProjPlanetParams p = P_PROJ_RING_CYL_DEFAULTS;
    p.ring_cyl.center_radius  = atof(opt_center_r->answer);
    p.ring_cyl.center_lon_deg = atof(opt_center_lon->answer);
    p.ring_cyl.clockwise_lon  = flag_cw->answer;

    PProjPlanet *proj = p_proj_planet_create(P_PROJ_RING_CYL, &p);
    if (!proj) G_fatal_error(_("Cannot create RingCylindrical projection"));

    int fd_in  = Rast_open_old(opt_input->answer, "");
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;
    DCELL *buf_in  = Rast_allocate_d_buf();
    DCELL *buf_out = Rast_allocate_d_buf();

    double *c1=(double*)G_malloc((size_t)ncols*sizeof(double));
    double *c2=(double*)G_malloc((size_t)ncols*sizeof(double));
    double *xo=(double*)G_malloc((size_t)ncols*sizeof(double));
    double *yo=(double*)G_malloc((size_t)ncols*sizeof(double));

    for(int row=0;row<nrows;row++){
        G_percent(row,nrows,2);
        Rast_get_d_row(fd_in,buf_in,row);
        for(int col=0;col<ncols;col++){
            double east  = reg.west  + (col+0.5)*reg.ew_res;
            double north = reg.north - (row+0.5)*reg.ns_res;
            /* Interpret east=ring_lon, north=ring_radius in input space */
            c1[col] = north; /* ring_radius */
            c2[col] = east;  /* ring_longitude */
        }
        if(flag_inv->answer)
            p_proj_planet_apply_row_inv(proj,ncols,c1,c2,xo,yo);
        else
            p_proj_planet_apply_row_fwd(proj,ncols,c1,c2,xo,yo);

        for(int col=0;col<ncols;col++){
            if(Rast_is_d_null_value(&buf_in[col]) || xo[col]!=xo[col])
                Rast_set_d_null_value(&buf_out[col],1);
            else
                buf_out[col]=buf_in[col]; /* pass pixel value through */
        }
        Rast_put_d_row(fd_out,buf_out);
    }
    G_percent(1,1,2);
    Rast_close(fd_in); Rast_close(fd_out);
    G_free(c1);G_free(c2);G_free(xo);G_free(yo);
    G_free(buf_in);G_free(buf_out);
    p_proj_planet_free(proj);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history);Rast_write_history(opt_output->answer,&history);
    G_message(_("Ring-plane projection complete: %s"),opt_output->answer);
    return EXIT_SUCCESS;
}
