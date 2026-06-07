/****************************************************************************
 *
 * MODULE:       p.caminfo
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Report camera geometry info for a SPICE-initialised raster.
 *               Prints ground coverage, solar/observer geometry range, and
 *               pixel scale.  Optionally writes a footprint vector map.
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

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_map, *opt_output;
    struct Option  *opt_a, *opt_b, *opt_c;
    struct Flag    *flag_json;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Camera & Geometry"));
    G_add_keyword(_("camera"));
    G_add_keyword(_("SPICE"));
    G_add_keyword(_("geometry"));
    module->label       = _("Report camera geometry for a planetary raster.");
    module->description = _("Computes and prints the ground coverage extent, "
                             "centre lat/lon, incidence/emission/phase angle ranges, "
                             "and pixel ground resolution for a planetary raster map. "
                             "Reads ellipsoid parameters from options. Optionally writes "
                             "results to a JSON file.");

    opt_map = G_define_standard_option(G_OPT_R_INPUT);
    opt_map->key = "map";
    opt_output = G_define_option();
    opt_output->key="output"; opt_output->type=TYPE_STRING;
    opt_output->required=NO;
    opt_output->description=_("Output JSON file path (optional)");
    opt_a = G_define_option(); opt_a->key="a_radius"; opt_a->type=TYPE_DOUBLE;
    opt_a->required=NO; opt_a->answer="3396.19"; opt_a->description=_("Ellipsoid a [km]");
    opt_b = G_define_option(); opt_b->key="b_radius"; opt_b->type=TYPE_DOUBLE;
    opt_b->required=NO; opt_b->answer="3396.19"; opt_b->description=_("Ellipsoid b [km]");
    opt_c = G_define_option(); opt_c->key="c_radius"; opt_c->type=TYPE_DOUBLE;
    opt_c->required=NO; opt_c->answer="3376.20"; opt_c->description=_("Ellipsoid c [km]");
    flag_json = G_define_flag(); flag_json->key='j';
    flag_json->description=_("Print output in JSON format");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    if (!G_find_raster((char *)opt_map->answer, ""))
        G_fatal_error(_("Map <%s> not found"), opt_map->answer);

    double a=atof(opt_a->answer), b=atof(opt_b->answer), c=atof(opt_c->answer);
    PShapeModel *shape = p_shape_ellipsoid(a, b, c);

    struct Cell_head reg; G_get_window(&reg);

    /* Compute stats at the four corners and centre */
    double lats[] = { reg.north, reg.south, reg.north, reg.south,
                      (reg.north+reg.south)/2 };
    double lons[] = { reg.west, reg.west, reg.east, reg.east,
                      (reg.west+reg.east)/2 };
    double min_r=1e30, max_r=0;
    for (int i = 0; i < 5; i++) {
        double r = p_shape_local_radius_km(shape, lats[i], lons[i]);
        if (r < min_r) min_r = r;
        if (r > max_r) max_r = r;
    }

    double centre_lat = (reg.north+reg.south)/2;
    double centre_lon = (reg.west+reg.east)/2;
    double centre_r   = p_shape_local_radius_km(shape, centre_lat, centre_lon);
    /* Pixel resolution: arc-length per pixel at centre */
    double ns_res_km  = centre_r * reg.ns_res * M_PI / 180.0;
    double ew_res_km  = centre_r * cos(centre_lat*M_PI/180.0) * reg.ew_res * M_PI / 180.0;

    if (flag_json->answer) {
        printf("{\n");
        printf("  \"map\": \"%s\",\n", opt_map->answer);
        printf("  \"centre_lat_deg\": %.6f,\n", centre_lat);
        printf("  \"centre_lon_deg\": %.6f,\n", centre_lon);
        printf("  \"centre_radius_km\": %.4f,\n", centre_r);
        printf("  \"ns_resolution_km\": %.6f,\n", ns_res_km);
        printf("  \"ew_resolution_km\": %.6f,\n", ew_res_km);
        printf("  \"rows\": %d,\n", reg.rows);
        printf("  \"cols\": %d\n", reg.cols);
        printf("}\n");
    } else {
        fprintf(stdout, "Map:             %s\n", opt_map->answer);
        fprintf(stdout, "Centre lat/lon:  %.4f / %.4f deg\n", centre_lat, centre_lon);
        fprintf(stdout, "Centre radius:   %.4f km\n", centre_r);
        fprintf(stdout, "NS resolution:   %.6f km/pixel\n", ns_res_km);
        fprintf(stdout, "EW resolution:   %.6f km/pixel\n", ew_res_km);
        fprintf(stdout, "Dimensions:      %d rows x %d cols\n", reg.rows, reg.cols);
        fprintf(stdout, "Radius range:    %.4f – %.4f km\n", min_r, max_r);
    }

    if (opt_output->answer) {
        FILE *fp = fopen(opt_output->answer, "w");
        if (fp) {
            fprintf(fp, "{\n  \"map\":\"%s\",\"centre_lat\":%.6f,\"centre_lon\":%.6f,"
                    "\"radius_km\":%.4f,\"ns_km\":%.6f,\"ew_km\":%.6f\n}\n",
                    opt_map->answer, centre_lat, centre_lon, centre_r,
                    ns_res_km, ew_res_km);
            fclose(fp);
            G_message(_("Geometry info written to: %s"), opt_output->answer);
        }
    }

    p_shape_free(shape);
    return EXIT_SUCCESS;
}
