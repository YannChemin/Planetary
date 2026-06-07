#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.slope.planet
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute slope and aspect for a planetary DEM using proper
 *               spherical/ellipsoidal geometry instead of assuming a flat
 *               Cartesian grid (as r.slope.aspect does on Earth).
 *
 *               Surface normals are derived from four-neighbour finite
 *               differences on the DEM, accounting for the lat/lon arc
 *               lengths at each pixel's latitude.
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
    struct Option  *opt_dem, *opt_slope, *opt_aspect;
    struct Option  *opt_radius, *opt_format;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Terrain Analysis"));
    G_add_keyword(_("terrain"));
    G_add_keyword(_("slope"));
    G_add_keyword(_("aspect"));
    module->label       = _("Compute slope and aspect for a planetary DEM.");
    module->description = _("Calculates slope (steepest descent angle from horizontal) "
                             "and aspect (compass direction of steepest descent) using "
                             "spherical arc-length distances between pixels. This is more "
                             "accurate than r.slope.aspect for planetary bodies where the "
                             "latitude-dependent arc lengths differ significantly. "
                             "Input DEM must be in height above reference sphere [m or km]. "
                             "Mean radius is used for arc-length scaling.");

    opt_dem    = G_define_standard_option(G_OPT_R_INPUT);
    opt_dem->key = "elevation"; opt_dem->description=_("Input DEM raster");
    opt_slope  = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_slope->key="slope"; opt_slope->required=NO;
    opt_slope->description=_("Output slope raster [degrees]");
    opt_aspect = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_aspect->key="aspect"; opt_aspect->required=NO;
    opt_aspect->description=_("Output aspect raster [degrees, N=0, E=90]");

    opt_radius = G_define_option(); opt_radius->key="radius";
    opt_radius->type=TYPE_DOUBLE; opt_radius->required=NO;
    opt_radius->answer="3390.0"; /* default: Mars mean radius */
    opt_radius->description=_("Mean planetary radius [same units as DEM heights]");

    opt_format = G_define_option(); opt_format->key="format";
    opt_format->type=TYPE_STRING; opt_format->required=NO;
    opt_format->answer="degrees"; opt_format->options="degrees,radians,percent";
    opt_format->description=_("Slope output format");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    if (!opt_slope->answer && !opt_aspect->answer)
        G_fatal_error(_("Specify at least one output: slope= or aspect="));

    double R = atof(opt_radius->answer);
    int out_slope  = (opt_slope->answer  != NULL);
    int out_aspect = (opt_aspect->answer != NULL);
    int fmt_pct    = strcmp(opt_format->answer,"percent")==0;
    int fmt_rad    = strcmp(opt_format->answer,"radians")==0;

    int fd_dem = Rast_open_old(opt_dem->answer, "");
    int fd_slp = out_slope  ? Rast_open_new(opt_slope->answer,  DCELL_TYPE) : -1;
    int fd_asp = out_aspect ? Rast_open_new(opt_aspect->answer, DCELL_TYPE) : -1;

    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    /* Load DEM into memory for 3-row sliding window */
    DCELL **dem = (DCELL **)G_malloc((size_t)nrows * sizeof(DCELL *));
    DCELL *tmp  = Rast_allocate_d_buf();
    for (int r=0; r<nrows; r++) {
        dem[r] = (DCELL *)G_malloc((size_t)ncols * sizeof(DCELL));
        Rast_get_d_row(fd_dem, tmp, r);
        memcpy(dem[r], tmp, (size_t)ncols * sizeof(DCELL));
    }
    Rast_close(fd_dem); G_free(tmp);

    DCELL *buf_slp = Rast_allocate_d_buf();
    DCELL *buf_asp = Rast_allocate_d_buf();

    for (int row=0; row<nrows; row++) {
        G_percent(row, nrows, 2);
        double lat_deg = reg.north - (row+0.5)*reg.ns_res;
        double lat_rad = lat_deg * DEG2RAD;

        /* Arc-length of one pixel */
        double dx = R * cos(lat_rad) * reg.ew_res * DEG2RAD; /* EW km */
        double dy = R * reg.ns_res   * DEG2RAD;              /* NS km */
        if (dx < 1e-10) dx = 1e-10;

        for (int col=0; col<ncols; col++) {
            /* Central pixel */
            if (Rast_is_d_null_value(&dem[row][col])) {
                if (fd_slp>=0) Rast_set_d_null_value(&buf_slp[col],1);
                if (fd_asp>=0) Rast_set_d_null_value(&buf_asp[col],1);
                continue;
            }
            /* 4-neighbour finite differences */
            int rN = (row>0)?row-1:row, rS = (row<nrows-1)?row+1:row;
            int cW = (col>0)?col-1:col, cE = (col<ncols-1)?col+1:col;

            double zN = dem[rN][col], zS = dem[rS][col];
            double zW = dem[row][cW], zE = dem[row][cE];
            if (Rast_is_d_null_value(&zN)||Rast_is_d_null_value(&zS)||
                Rast_is_d_null_value(&zW)||Rast_is_d_null_value(&zE)) {
                if (fd_slp>=0) Rast_set_d_null_value(&buf_slp[col],1);
                if (fd_asp>=0) Rast_set_d_null_value(&buf_asp[col],1);
                continue;
            }
            double dz_dy = (zN - zS) / (2.0 * dy); /* +N direction */
            double dz_dx = (zE - zW) / (2.0 * dx); /* +E direction */

            double slope_rad = atan(sqrt(dz_dx*dz_dx + dz_dy*dz_dy));

            if (fd_slp>=0) {
                if (fmt_rad)
                    buf_slp[col] = slope_rad;
                else if (fmt_pct)
                    buf_slp[col] = tan(slope_rad) * 100.0;
                else
                    buf_slp[col] = slope_rad * RAD2DEG;
            }
            if (fd_asp>=0) {
                /* Aspect: degrees from N, clockwise (N=0, E=90) */
                double asp_rad = atan2(dz_dx, dz_dy); /* E of N convention */
                double asp_deg = asp_rad * RAD2DEG;
                if (asp_deg < 0.0) asp_deg += 360.0;
                buf_asp[col] = asp_deg;
            }
        }
        if (fd_slp>=0) Rast_put_d_row(fd_slp, buf_slp);
        if (fd_asp>=0) Rast_put_d_row(fd_asp, buf_asp);
    }
    G_percent(1,1,2);

    for (int r=0; r<nrows; r++) G_free(dem[r]);
    G_free(dem); G_free(buf_slp); G_free(buf_asp);

    if (fd_slp>=0) {
        Rast_close(fd_slp);
        Rast_short_history(opt_slope->answer,"raster",&history);
        Rast_command_history(&history); Rast_write_history(opt_slope->answer,&history);
    }
    if (fd_asp>=0) {
        Rast_close(fd_asp);
        Rast_short_history(opt_aspect->answer,"raster",&history);
        Rast_command_history(&history); Rast_write_history(opt_aspect->answer,&history);
    }
    G_message(_("p.slope.planet complete."));
    return EXIT_SUCCESS;
}
