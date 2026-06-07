/****************************************************************************
 *
 * MODULE:       p.target.info
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Display physical constants for a named solar system body.
 *               Reports radii, GM, rotation rate, reference frame, and
 *               recommends a map projection and GRASS location settings.
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
#include <grass/glocale.h>

typedef struct {
    const char *name;
    const char *naif_id;
    double a_km, b_km, c_km; /* triaxial radii */
    double GM;               /* km^3/s^2 */
    double rot_period_hr;    /* sidereal rotation period */
    const char *frame;       /* NAIF body-fixed frame */
    const char *proj_rec;    /* recommended GRASS projection */
} BodyInfo;

static const BodyInfo BODIES[] = {
    { "Mercury", "199",  2439.7,  2439.7,  2438.3,   22032.1,  1407.6, "IAU_MERCURY", "Equirectangular" },
    { "Venus",   "299",  6051.8,  6051.8,  6051.8,  324859.0, -5832.6, "IAU_VENUS",   "Equirectangular" },
    { "Moon",    "301",  1737.4,  1737.4,  1735.0,    4902.8,    655.7, "MOON_ME",     "Sinusoidal" },
    { "Mars",    "499",  3396.19, 3396.19, 3376.20,  42828.4,   24.62, "IAU_MARS",   "Sinusoidal" },
    { "Phobos",  "401",    13.0,   11.4,    9.1,        7.158e-4, 7.65, "IAU_PHOBOS", "Stereographic" },
    { "Deimos",  "402",     7.8,    6.0,    5.1,        9.615e-5, 30.3, "IAU_DEIMOS", "Stereographic" },
    { "Vesta",   "2000004",278.6,  249.2,  226.2,   17.288,     5.342, "IAU_VESTA",  "Sinusoidal" },
    { "Ceres",   "2000001",487.3,  487.3,  454.7,   62.629,     9.074, "IAU_CERES",  "Sinusoidal" },
    { "Enceladus","602",   256.6,  251.4,  248.3,    7.211,     32.9,  "IAU_ENCELADUS","Stereographic"},
    { "Titan",   "606",  2574.9, 2574.9, 2574.9,  8978.14,    382.7,  "IAU_TITAN",  "Equirectangular"},
    { "Pluto",   "999",  1188.3, 1188.3, 1188.3,   869.6,    -153.3,  "IAU_PLUTO",  "Stereographic" },
    { NULL, NULL, 0,0,0, 0, 0, NULL, NULL }
};

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_body;
    struct Flag    *flag_json, *flag_gproj;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Utilities"));
    G_add_keyword(_("body constants"));
    G_add_keyword(_("SPICE"));
    G_add_keyword(_("target"));
    module->label = _("Display physical constants for a solar system body.");
    module->description = _("Reports radii, GM, rotation period, NAIF body-fixed "
                             "frame name, and recommended GRASS map projection for "
                             "a named planetary body. "
                             "Use -j for JSON output or -g to print g.proj parameters.");

    opt_body = G_define_option(); opt_body->key="body";
    opt_body->type=TYPE_STRING; opt_body->required=YES;
    opt_body->options="Mercury,Venus,Moon,Mars,Phobos,Deimos,Vesta,Ceres,"
                       "Enceladus,Titan,Pluto";
    opt_body->description=_("Target body name");
    flag_json  = G_define_flag(); flag_json->key='j';
    flag_json->description=_("Print JSON output");
    flag_gproj = G_define_flag(); flag_gproj->key='g';
    flag_gproj->description=_("Print g.proj command for this body");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *bname = opt_body->answer;
    const BodyInfo *bi = NULL;
    for(int i=0; BODIES[i].name; i++)
        if(G_strcasecmp(BODIES[i].name,bname)==0){bi=&BODIES[i];break;}
    if(!bi) G_fatal_error(_("Unknown body '%s'"),bname);

    double mean_r = (bi->a_km+bi->b_km+bi->c_km)/3.0;
    double flat   = (bi->a_km - bi->c_km)/bi->a_km;

    if(flag_json->answer){
        printf("{\n");
        printf("  \"name\": \"%s\",\n", bi->name);
        printf("  \"naif_id\": \"%s\",\n", bi->naif_id);
        printf("  \"a_km\": %.4f,\n", bi->a_km);
        printf("  \"b_km\": %.4f,\n", bi->b_km);
        printf("  \"c_km\": %.4f,\n", bi->c_km);
        printf("  \"mean_radius_km\": %.4f,\n", mean_r);
        printf("  \"flattening\": %.6f,\n", flat);
        printf("  \"GM_km3_s2\": %.4f,\n", bi->GM);
        printf("  \"rot_period_hr\": %.4f,\n", bi->rot_period_hr);
        printf("  \"naif_frame\": \"%s\",\n", bi->frame);
        printf("  \"recommended_projection\": \"%s\"\n", bi->proj_rec);
        printf("}\n");
    } else if(flag_gproj->answer){
        printf("# g.proj for %s\n", bi->name);
        printf("g.proj -c proj=eqc a=%g b=%g "
               "lat_ts=0 lon_0=0 x_0=0 y_0=0 datum=none\n",
               bi->a_km*1000.0, bi->c_km*1000.0);
    } else {
        printf("Body:            %s  (NAIF %s)\n", bi->name, bi->naif_id);
        printf("Radii (a,b,c):   %.3f × %.3f × %.3f km\n",bi->a_km,bi->b_km,bi->c_km);
        printf("Mean radius:     %.3f km\n", mean_r);
        printf("Flattening:      %.6f\n", flat);
        printf("GM:              %.4f km³/s²\n", bi->GM);
        printf("Rotation period: %.3f h  (%s)\n",
               fabs(bi->rot_period_hr),
               bi->rot_period_hr<0?"retrograde":"prograde");
        printf("NAIF frame:      %s\n", bi->frame);
        printf("Recommended proj:%s\n", bi->proj_rec);
    }
    return EXIT_SUCCESS;
}
