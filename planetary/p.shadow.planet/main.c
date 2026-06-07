#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.shadow.planet
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute a shadow mask for a planetary DEM given solar geometry.
 *               A pixel is in shadow when the sun ray from that pixel to the
 *               solar position intersects the DEM before reaching the sun.
 *               Uses a simple ray-marching approach along the solar azimuth.
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

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_dem, *opt_output;
    struct Option  *opt_solar_az, *opt_solar_el;
    struct Option  *opt_radius;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Terrain Analysis"));
    G_add_keyword(_("shadow"));
    G_add_keyword(_("terrain"));
    G_add_keyword(_("solar"));
    module->label       = _("Compute a shadow mask for a planetary DEM.");
    module->description = _("Generates a binary shadow mask (1=sunlit, 0=shadow) for "
                             "a planetary elevation raster given the solar azimuth and "
                             "elevation angles. Uses ray-marching along the solar direction. "
                             "Input DEM should be height above reference [m or km]; "
                             "consistent units with planet radius.");

    opt_dem    = G_define_standard_option(G_OPT_R_INPUT);
    opt_dem->key = "elevation"; opt_dem->description=_("Input height-above-reference DEM");
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_solar_az = G_define_option(); opt_solar_az->key="solar_azimuth";
    opt_solar_az->type=TYPE_DOUBLE; opt_solar_az->required=YES;
    opt_solar_az->description=_("Solar azimuth [degrees, N=0, E=90]");

    opt_solar_el = G_define_option(); opt_solar_el->key="solar_elevation";
    opt_solar_el->type=TYPE_DOUBLE; opt_solar_el->required=YES;
    opt_solar_el->description=_("Solar elevation angle above horizon [degrees, 0–90]");

    opt_radius = G_define_option(); opt_radius->key="radius";
    opt_radius->type=TYPE_DOUBLE; opt_radius->required=NO;
    opt_radius->answer="3390000.0"; /* metres */
    opt_radius->description=_("Mean planetary radius [same units as DEM heights]");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    double sol_az_deg = atof(opt_solar_az->answer);
    double sol_el_deg = atof(opt_solar_el->answer);
    double R          = atof(opt_radius->answer);

    if (sol_el_deg <= 0.0) {
        G_warning(_("Solar elevation <= 0 deg: entire scene is in shadow."));
    }

    int fd_dem = Rast_open_old(opt_dem->answer, "");
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    DCELL **dem = (DCELL **)G_malloc((size_t)nrows * sizeof(DCELL *));
    DCELL *tmp  = Rast_allocate_d_buf();
    for (int r=0; r<nrows; r++) {
        dem[r] = (DCELL *)G_malloc((size_t)ncols * sizeof(DCELL));
        Rast_get_d_row(fd_dem, tmp, r);
        memcpy(dem[r], tmp, (size_t)ncols * sizeof(DCELL));
    }
    Rast_close(fd_dem); G_free(tmp);

    /* Solar direction vector in pixel space.
     * az = N=0, E=90 → col direction = +sin(az), row direction = -cos(az) */
    double sol_az_rad = sol_az_deg * DEG2RAD;
    double sol_el_rad = sol_el_deg * DEG2RAD;
    double dc = sin(sol_az_rad);  /* col step per pixel along ray */
    double dr = -cos(sol_az_rad); /* row step per pixel along ray */
    double de = tan(sol_el_rad);  /* height gain per horizontal pixel */

    int fd_out = Rast_open_new(opt_output->answer, CELL_TYPE);
    CELL *buf_out = Rast_allocate_c_buf();

    for (int row=0; row<nrows; row++) {
        G_percent(row, nrows, 2);
        for (int col=0; col<ncols; col++) {
            double z0;
            if (Rast_is_d_null_value(&dem[row][col])) {
                Rast_set_c_null_value(&buf_out[col], 1); continue;
            }
            z0 = dem[row][col];

            if (sol_el_deg <= 0.0) { buf_out[col] = 0; continue; }

            /* March ray toward the sun; check if DEM blocks the ray */
            int in_shadow = 0;
            double step = 1.0; /* step size in pixels */
            double max_dist = (double)(nrows > ncols ? nrows : ncols) * 1.5;
            for (double dist = step; dist < max_dist; dist += step) {
                int rc = (int)(row + dr * dist + 0.5);
                int cc = (int)(col + dc * dist + 0.5);
                if (rc < 0 || rc >= nrows || cc < 0 || cc >= ncols) break;
                if (Rast_is_d_null_value(&dem[rc][cc])) break;
                /* Height of sun ray at this distance */
                double z_ray = z0 + de * dist;
                if (dem[rc][cc] > z_ray) { in_shadow = 1; break; }
            }
            buf_out[col] = in_shadow ? 0 : 1;
        }
        Rast_put_c_row(fd_out, buf_out);
    }
    G_percent(1,1,2);

    for (int r=0; r<nrows; r++) G_free(dem[r]);
    G_free(dem); G_free(buf_out);
    Rast_close(fd_out);

    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);

    G_message(_("Shadow mask written: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
