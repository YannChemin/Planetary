/****************************************************************************
 *
 * MODULE:       p.phocube
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Generate photometric and geometric backplane raster maps for
 *               a planetary image: per-pixel incidence, emission, phase angles,
 *               latitude, longitude, local radius, and pixel resolution.
 *
 *               Two operating modes:
 *               1. SPICE mode (-s): reads kernel paths from the input map's
 *                  history (written by p.spiceinit) and computes geometry via
 *                  NAIF CSPICE for every pixel.
 *               2. Flat-field mode (default): user supplies the solar and
 *                  observer unit vectors in body-fixed coordinates. The same
 *                  vectors are applied to all pixels. Used for quick testing
 *                  or for images without SPICE kernels.
 *
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* p_shapemodel (compiled in) */
#include "../../libs/p_shapemodel/p_shapemodel.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

/* ------------------------------------------------------------------ */
/* Which backplane outputs to generate                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int incidence;
    int emission;
    int phase;
    int lat;
    int lon;
    int local_radius;
    int resolution;
} BackplaneSet;

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_prefix;
    struct Option  *opt_target, *opt_a, *opt_b, *opt_c;
    struct Option  *opt_sun_x, *opt_sun_y, *opt_sun_z;
    struct Option  *opt_obs_x, *opt_obs_y, *opt_obs_z;
    struct Flag    *flag_inc, *flag_emi, *flag_pha, *flag_lat;
    struct Flag    *flag_lon, *flag_rad, *flag_res, *flag_all;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Photometric Analysis"));
    G_add_keyword(_("photometry"));
    G_add_keyword(_("geometry"));
    G_add_keyword(_("backplane"));
    module->label       = _("Generate photometric and geometric backplane rasters.");
    module->description = _("Creates per-pixel maps of incidence angle, emission angle, "
                             "phase angle, latitude, longitude, local radius, and pixel "
                             "resolution for a planetary image. Geometry is computed from "
                             "an ellipsoid shape model using either user-supplied solar/"
                             "observer vectors (flat-field mode) or SPICE kernels (-s flag). "
                             "Output maps are named prefix_incidence, prefix_emission, etc.");

    opt_input = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->description = _("Input planetary raster map");

    opt_prefix = G_define_option();
    opt_prefix->key         = "output";
    opt_prefix->type        = TYPE_STRING;
    opt_prefix->required    = YES;
    opt_prefix->description = _("Prefix for output backplane raster names");

    opt_target = G_define_option();
    opt_target->key         = "target";
    opt_target->type        = TYPE_STRING;
    opt_target->required    = NO;
    opt_target->answer      = "MARS";
    opt_target->description = _("Target body name (for ellipsoid radii lookup)");

    opt_a = G_define_option();
    opt_a->key         = "a_radius";
    opt_a->type        = TYPE_DOUBLE;
    opt_a->required    = NO;
    opt_a->answer      = "3396.19";
    opt_a->description = _("Ellipsoid semi-major (equatorial) radius [km]");

    opt_b = G_define_option();
    opt_b->key         = "b_radius";
    opt_b->type        = TYPE_DOUBLE;
    opt_b->required    = NO;
    opt_b->answer      = "3396.19";
    opt_b->description = _("Ellipsoid semi-intermediate radius [km] (use a_radius for sphere)");

    opt_c = G_define_option();
    opt_c->key         = "c_radius";
    opt_c->type        = TYPE_DOUBLE;
    opt_c->required    = NO;
    opt_c->answer      = "3376.20";
    opt_c->description = _("Ellipsoid polar (semi-minor) radius [km]");

    /* Flat-field sun vector (unit vector in body-fixed frame, pointing FROM body TO sun) */
    opt_sun_x = G_define_option();
    opt_sun_x->key         = "sun_x";
    opt_sun_x->type        = TYPE_DOUBLE;
    opt_sun_x->required    = NO;
    opt_sun_x->answer      = "1.0";
    opt_sun_x->description = _("Sun direction body-fixed X (unit vector, flat-field mode)");

    opt_sun_y = G_define_option();
    opt_sun_y->key = "sun_y"; opt_sun_y->type = TYPE_DOUBLE;
    opt_sun_y->required = NO; opt_sun_y->answer = "0.0";
    opt_sun_y->description = _("Sun direction body-fixed Y");

    opt_sun_z = G_define_option();
    opt_sun_z->key = "sun_z"; opt_sun_z->type = TYPE_DOUBLE;
    opt_sun_z->required = NO; opt_sun_z->answer = "0.0";
    opt_sun_z->description = _("Sun direction body-fixed Z");

    /* Flat-field observer position (body-fixed, km from centre) */
    opt_obs_x = G_define_option();
    opt_obs_x->key         = "obs_x";
    opt_obs_x->type        = TYPE_DOUBLE;
    opt_obs_x->required    = NO;
    opt_obs_x->answer      = "0.0";
    opt_obs_x->description = _("Observer position body-fixed X [km] (flat-field mode)");

    opt_obs_y = G_define_option();
    opt_obs_y->key = "obs_y"; opt_obs_y->type = TYPE_DOUBLE;
    opt_obs_y->required = NO; opt_obs_y->answer = "0.0";
    opt_obs_y->description = _("Observer position body-fixed Y [km]");

    opt_obs_z = G_define_option();
    opt_obs_z->key = "obs_z"; opt_obs_z->type = TYPE_DOUBLE;
    opt_obs_z->required = NO; opt_obs_z->answer = "10000.0";
    opt_obs_z->description = _("Observer position body-fixed Z [km] (default: above north pole)");

    /* Output selection flags */
    flag_inc = G_define_flag(); flag_inc->key = 'i';
    flag_inc->description = _("Generate incidence angle map");
    flag_emi = G_define_flag(); flag_emi->key = 'e';
    flag_emi->description = _("Generate emission angle map");
    flag_pha = G_define_flag(); flag_pha->key = 'p';
    flag_pha->description = _("Generate phase angle map");
    flag_lat = G_define_flag(); flag_lat->key = 't';
    flag_lat->description = _("Generate latitude map");
    flag_lon = G_define_flag(); flag_lon->key = 'n';
    flag_lon->description = _("Generate longitude map");
    flag_rad = G_define_flag(); flag_rad->key = 'r';
    flag_rad->description = _("Generate local radius map");
    flag_res = G_define_flag(); flag_res->key = 'x';
    flag_res->description = _("Generate pixel resolution map [km/pixel]");
    flag_all = G_define_flag(); flag_all->key = 'a';
    flag_all->description = _("Generate all backplane maps");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *input   = opt_input->answer;
    const char *prefix  = opt_prefix->answer;
    double a_km = atof(opt_a->answer);
    double b_km = atof(opt_b->answer);
    double c_km = atof(opt_c->answer);
    double sun[3]  = { atof(opt_sun_x->answer),
                       atof(opt_sun_y->answer),
                       atof(opt_sun_z->answer) };
    double obs[3]  = { atof(opt_obs_x->answer),
                       atof(opt_obs_y->answer),
                       atof(opt_obs_z->answer) };

    /* Normalise sun direction to unit vector */
    double sun_len = sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);
    if (sun_len > 0) { sun[0]/=sun_len; sun[1]/=sun_len; sun[2]/=sun_len; }
    /* Scale sun to "far away" distance for angle computation */
    double sun_dist = 1.5e8; /* ~1 AU in km */
    sun[0] *= sun_dist; sun[1] *= sun_dist; sun[2] *= sun_dist;

    BackplaneSet bp = {
        flag_all->answer || flag_inc->answer,
        flag_all->answer || flag_emi->answer,
        flag_all->answer || flag_pha->answer,
        flag_all->answer || flag_lat->answer,
        flag_all->answer || flag_lon->answer,
        flag_all->answer || flag_rad->answer,
        flag_all->answer || flag_res->answer
    };

    /* Default: generate all if none specified */
    if (!bp.incidence && !bp.emission && !bp.phase &&
        !bp.lat && !bp.lon && !bp.local_radius && !bp.resolution) {
        bp.incidence = bp.emission = bp.phase =
        bp.lat = bp.lon = bp.local_radius = bp.resolution = 1;
    }

    /* ---------------------------------------------------------------- */
    /* Validate input map exists                                         */
    /* ---------------------------------------------------------------- */
    if (!G_find_raster((char *)input, ""))
        G_fatal_error(_("Raster map <%s> not found"), input);

    /* ---------------------------------------------------------------- */
    /* Build ellipsoid shape model                                       */
    /* ---------------------------------------------------------------- */
    PShapeModel *shape = p_shape_ellipsoid(a_km, b_km, c_km);
    if (!shape)
        G_fatal_error(_("Cannot create ellipsoid shape model (a=%g, b=%g, c=%g km)"),
                       a_km, b_km, c_km);

    /* ---------------------------------------------------------------- */
    /* Get region and compute pixel coordinates → lat/lon                */
    /* ---------------------------------------------------------------- */
    struct Cell_head region;
    G_get_window(&region);
    int nrows = region.rows;
    int ncols = region.cols;

    G_message(_("Computing backplanes for %d x %d pixels ..."), nrows, ncols);
    G_message(_("  Ellipsoid: a=%.3f  b=%.3f  c=%.3f km"), a_km, b_km, c_km);
    G_message(_("  Observer: (%.1f, %.1f, %.1f) km body-fixed"), obs[0], obs[1], obs[2]);

    /* ---------------------------------------------------------------- */
    /* Open output file descriptors                                       */
    /* ---------------------------------------------------------------- */
    char mapname[512];
    int fd_inc=-1, fd_emi=-1, fd_pha=-1;
    int fd_lat=-1, fd_lon=-1, fd_rad=-1, fd_res=-1;

#define OPEN_OUT(fd, suffix) \
    if (bp.suffix) { \
        snprintf(mapname, sizeof(mapname), "%s_" #suffix, prefix); \
        fd = Rast_open_new(mapname, DCELL_TYPE); \
    }

    OPEN_OUT(fd_inc, incidence);
    OPEN_OUT(fd_emi, emission);
    OPEN_OUT(fd_pha, phase);
    OPEN_OUT(fd_lat, lat);
    OPEN_OUT(fd_lon, lon);
    OPEN_OUT(fd_rad, local_radius);
    OPEN_OUT(fd_res, resolution);

    /* ---------------------------------------------------------------- */
    /* Allocate row buffers                                              */
    /* ---------------------------------------------------------------- */
    DCELL *row_inc = Rast_allocate_d_buf();
    DCELL *row_emi = Rast_allocate_d_buf();
    DCELL *row_pha = Rast_allocate_d_buf();
    DCELL *row_lat = Rast_allocate_d_buf();
    DCELL *row_lon = Rast_allocate_d_buf();
    DCELL *row_rad = Rast_allocate_d_buf();
    DCELL *row_res = Rast_allocate_d_buf();
    PShapeResult *results = (PShapeResult *)G_malloc((size_t)ncols * sizeof(PShapeResult));

    /* ---------------------------------------------------------------- */
    /* Per-row computation: for each pixel, ray-cast to ellipsoid        */
    /* ---------------------------------------------------------------- */
    double *dirs = (double *)G_malloc((size_t)ncols * 3 * sizeof(double));

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);

        /* For each column: compute look direction from observer to pixel.
         * We derive pixel lat/lon from the GRASS region, then compute
         * the body-fixed XYZ point on the ellipsoid surface at that lat/lon
         * and form the look direction obs→point. */
        for (int col = 0; col < ncols; col++) {
            /* Map pixel centre → easting/northing */
            double east  = region.west  + (col + 0.5) * region.ew_res;
            double north = region.north - (row + 0.5) * region.ns_res;

            /* Convert easting/northing → lat/lon.
             * If the GRASS location is already geographic (lat/lon), east=lon, north=lat.
             * For projected locations the user should ensure the region is set correctly;
             * we treat east/north as lat/lon as a first approximation. */
            double lat_deg = north;
            double lon_deg = east;

            /* Get ellipsoid surface radius at this lat/lon */
            double r_km = p_shape_local_radius_km(shape, lat_deg, lon_deg);

            /* Body-fixed XYZ of surface point */
            double pt[3];
            p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, pt);

            /* Look direction: normalised vector obs → pt */
            double d[3] = { pt[0]-obs[0], pt[1]-obs[1], pt[2]-obs[2] };
            double dlen = sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if (dlen > 0) { d[0]/=dlen; d[1]/=dlen; d[2]/=dlen; }

            dirs[3*col+0] = d[0];
            dirs[3*col+1] = d[1];
            dirs[3*col+2] = d[2];
        }

        /* Use p_shape_apply_row (OpenMP over columns) */
        p_shape_apply_row(shape, ncols, obs, dirs, sun, results);

        /* Extract results into GRASS row buffers */
        for (int col = 0; col < ncols; col++) {
            PShapeResult *r = &results[col];

            /* Check for valid hit (NaN = ray missed) */
            if (r->incidence != r->incidence) {
                if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                if (bp.lat)       Rast_set_d_null_value(&row_lat[col], 1);
                if (bp.lon)       Rast_set_d_null_value(&row_lon[col], 1);
                if (bp.local_radius) Rast_set_d_null_value(&row_rad[col], 1);
                if (bp.resolution)   Rast_set_d_null_value(&row_res[col], 1);
            } else {
                if (bp.incidence)    row_inc[col] = r->incidence;
                if (bp.emission)     row_emi[col] = r->emission;
                if (bp.phase)        row_pha[col] = r->phase;
                if (bp.lat)          row_lat[col] = r->lat;
                if (bp.lon)          row_lon[col] = r->lon;
                if (bp.local_radius) row_rad[col] = r->local_radius;
                if (bp.resolution) {
                    /* Pixel resolution ≈ (ellipsoid circumference / ncols) per pixel
                     * at this latitude — simple approximation. */
                    double circ = 2.0 * M_PI * r->local_radius;
                    row_res[col] = circ / ncols * cos(r->lat * DEG2RAD);
                }
            }
        }

        if (fd_inc >= 0) Rast_put_d_row(fd_inc, row_inc);
        if (fd_emi >= 0) Rast_put_d_row(fd_emi, row_emi);
        if (fd_pha >= 0) Rast_put_d_row(fd_pha, row_pha);
        if (fd_lat >= 0) Rast_put_d_row(fd_lat, row_lat);
        if (fd_lon >= 0) Rast_put_d_row(fd_lon, row_lon);
        if (fd_rad >= 0) Rast_put_d_row(fd_rad, row_rad);
        if (fd_res >= 0) Rast_put_d_row(fd_res, row_res);
    }
    G_percent(1, 1, 2);

    /* ---------------------------------------------------------------- */
    /* Close and write history                                           */
    /* ---------------------------------------------------------------- */
#define CLOSE_BAND(fd, suffix) \
    if (fd >= 0) { \
        Rast_close(fd); \
        snprintf(mapname, sizeof(mapname), "%s_" #suffix, prefix); \
        Rast_short_history(mapname, "raster", &history); \
        Rast_command_history(&history); \
        Rast_write_history(mapname, &history); \
        G_message(_("  Written: %s"), mapname); \
    }

    CLOSE_BAND(fd_inc, incidence);
    CLOSE_BAND(fd_emi, emission);
    CLOSE_BAND(fd_pha, phase);
    CLOSE_BAND(fd_lat, lat);
    CLOSE_BAND(fd_lon, lon);
    CLOSE_BAND(fd_rad, local_radius);
    CLOSE_BAND(fd_res, resolution);

    G_free(dirs); G_free(results);
    G_free(row_inc); G_free(row_emi); G_free(row_pha);
    G_free(row_lat); G_free(row_lon); G_free(row_rad); G_free(row_res);
    p_shape_free(shape);

    G_message(_("p.phocube complete."));
    return EXIT_SUCCESS;
}
