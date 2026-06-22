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
#include <ctype.h>
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/gprojects.h>
#include <grass/glocale.h>

/* p_shapemodel (compiled in) */
#include "../../libs/p_shapemodel/p_shapemodel.h"
/* p_spice (compiled in) -- SPICE mode (-s) only */
#include "../../libs/p_spice/p_spice.h"
/* p_meta (compiled in) -- camera mode (-c) instrument= auto-detection */
#include "../../libs/p_meta/p_meta.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

/* SPICE history metadata, written by p.spiceinit as one
 * "SPICE_<KEY>=<value>" line per call to Rast_append_history(). Kernel
 * paths may be semicolon-separated (multiple files per type); TARGET/
 * OBSERVER/TIME/LINE_RATE are single values. Last occurrence of a given
 * key wins (handles a raster having been re-spiceinit'd). LINE_RATE is
 * optional (seconds/row); when absent, -s mode uses a single mid-scene
 * epoch (TIME) for every row -- see p.phocube.md NOTES. */
#define SPICE_META_PREFIX "SPICE_"

typedef struct {
    char target[64];
    char observer[64];
    char time[64];
    double line_rate;  /* seconds per output row; 0 = single mid-scene epoch */
    int  have_target, have_observer, have_time, have_line_rate, have_dsk;
    int  n_kernels_loaded;
} SpiceHistoryInfo;

static void spice_load_paths(const char *value, int *n_loaded)
{
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s", value);
    char *tok = strtok(buf, ";");
    while (tok) {
        if (p_spice_load(tok) < 0)
            G_warning(_("SPICE: failed to load kernel '%s'"), tok);
        else
            (*n_loaded)++;
        tok = strtok(NULL, ";");
    }
}

/* Read every SPICE_* key written by p.spiceinit from the input map's
 * history, loading kernels as they're found. G_fatal_error()s if no
 * target/observer/time is found -- -s mode cannot proceed without them. */
static SpiceHistoryInfo read_spice_history(const char *mapname, const char *mapset)
{
    SpiceHistoryInfo info;
    memset(&info, 0, sizeof(info));

    struct History hist;
    if (Rast_read_history(mapname, mapset, &hist) < 0)
        G_fatal_error(_("No history metadata on <%s> -- run p.spiceinit first."),
                       mapname);

    int nlines = Rast_history_length(&hist);
    for (int i = 0; i < nlines; i++) {
        const char *line = Rast_history_line(&hist, i);
        if (strncmp(line, SPICE_META_PREFIX, strlen(SPICE_META_PREFIX)) != 0)
            continue;
        const char *kv = line + strlen(SPICE_META_PREFIX);
        const char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        char key[32];
        size_t keylen = (size_t)(eq - kv);
        if (keylen >= sizeof(key))
            continue;
        memcpy(key, kv, keylen);
        key[keylen] = '\0';
        const char *value = eq + 1;

        if (strcmp(key, "TARGET") == 0) {
            snprintf(info.target, sizeof(info.target), "%s", value);
            info.have_target = 1;
        }
        else if (strcmp(key, "OBSERVER") == 0) {
            snprintf(info.observer, sizeof(info.observer), "%s", value);
            info.have_observer = 1;
        }
        else if (strcmp(key, "TIME") == 0) {
            snprintf(info.time, sizeof(info.time), "%s", value);
            info.have_time = 1;
        }
        else if (strcmp(key, "LINE_RATE") == 0) {
            info.line_rate = atof(value);
            info.have_line_rate = 1;
        }
        else if (strcmp(key, "DSK") == 0) {
            int n_before = info.n_kernels_loaded;
            spice_load_paths(value, &info.n_kernels_loaded);
            if (info.n_kernels_loaded > n_before)
                info.have_dsk = 1;
        }
        else if (strcmp(key, "LSK") == 0 || strcmp(key, "SCLK") == 0 ||
                 strcmp(key, "CK") == 0 || strcmp(key, "SPK") == 0 ||
                 strcmp(key, "IK") == 0 || strcmp(key, "FK") == 0 ||
                 strcmp(key, "PCK") == 0) {
            spice_load_paths(value, &info.n_kernels_loaded);
        }
    }
    Rast_free_history(&hist);

    if (!info.have_target || !info.have_observer || !info.have_time)
        G_fatal_error(_("SPICE mode (-s) requires target=, observer= and "
                        "time= to have been attached to <%s> via "
                        "p.spiceinit (found: target=%s observer=%s time=%s)."),
                       mapname,
                       info.have_target ? info.target : "(missing)",
                       info.have_observer ? info.observer : "(missing)",
                       info.have_time ? info.time : "(missing)");
    if (info.n_kernels_loaded == 0)
        G_fatal_error(_("SPICE mode (-s) found no loadable kernels in <%s>'s "
                        "history -- run p.spiceinit with lsk=/spk=/etc first."),
                       mapname);

    return info;
}

static void uppercase_copy(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Camera mode (-c): CRISM-specific instrument camera model            */
/*                                                                      */
/* CRISM (VNIR detector -74017 / IR detector -74018) is a pushbroom    */
/* imaging spectrometer: the cross-track (slit) angle of a given       */
/* (band, sample) pixel relative to the instrument boresight is given  */
/* by a real, documented linear model from the IK itself --            */
/* "line_of_sight_angle = a0(band) + a1(band)*line_sample" -- read     */
/* directly from NAIF's mro_crism_v10.ti. Geometry is computed once at */
/* one reference band for the whole cube (the real CRISM DDR           */
/* convention -- per-band "keystone" variation is tiny), not per band. */
/* ------------------------------------------------------------------ */
#define CRISM_MAX_CAMERA_COEFF_VALS 1500  /* >= 480 bands * 3 columns */

typedef struct {
    int    naif_id;
    char   frame[64];
    double boresight[3];
    double slit_dir[3];
    double a0, a1;     /* camera-model coefficients for the chosen band */
    int    band;
} CrismCameraModel;

static void load_crism_camera_model(const char *instrument, int band_opt,
                                     int have_band_opt, CrismCameraModel *cam)
{
    memset(cam, 0, sizeof(*cam));

    if (strcmp(instrument, "CRISM_VNIR") == 0) {
        cam->naif_id = -74017;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_VNIR");
    }
    else if (strcmp(instrument, "CRISM_IR") == 0) {
        cam->naif_id = -74018;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_IR");
    }
    else
        G_fatal_error(_("Camera mode (-c): unsupported instrument='%s' "
                        "(v1 supports only CRISM_VNIR, CRISM_IR)."), instrument);

    char varname[80];
    int n;

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 3, &n, cam->boresight) < 0 || n != 3)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                        "IK -- has the right CRISM instrument kernel been "
                        "attached via p.spiceinit's ik= option?"), varname);

    snprintf(varname, sizeof(varname), "INS%d_SLIT_DIRECTION", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 3, &n, cam->slit_dir) < 0 || n != 3)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    if (have_band_opt)
        cam->band = band_opt;
    else {
        double refband;
        snprintf(varname, sizeof(varname), "INS%d_REFERENCE_BAND", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &refband) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): could not read %s from the "
                            "loaded IK, and band= was not given."), varname);
        cam->band = (int)refband;
    }

    double coeff[CRISM_MAX_CAMERA_COEFF_VALS];
    snprintf(varname, sizeof(varname), "INS%d_CAMERA_COEFF", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, CRISM_MAX_CAMERA_COEFF_VALS, &n, coeff) < 0)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    int found = 0;
    for (int i = 0; i + 2 < n; i += 3) {
        if ((int)coeff[i] == cam->band) {
            cam->a0 = coeff[i + 1];
            cam->a1 = coeff[i + 2];
            found = 1;
            break;
        }
    }
    if (!found)
        G_fatal_error(_("Camera mode (-c): band %d not found in %s "
                        "(valid range is whatever the loaded IK defines)."),
                       cam->band, varname);
}

/* Rodrigues' rotation formula: rotate vector v by angle theta (radians)
 * about unit axis (normalised internally). Pure vector math -- no
 * CSPICE call needed, the axis/vector are already in the same frame. */
static void rodrigues_rotate(const double v[3], const double axis_in[3],
                              double theta, double out[3])
{
    double axis[3] = { axis_in[0], axis_in[1], axis_in[2] };
    double alen = sqrt(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
    if (alen > 0) { axis[0] /= alen; axis[1] /= alen; axis[2] /= alen; }

    double c = cos(theta), s = sin(theta);
    double dot = v[0]*axis[0] + v[1]*axis[1] + v[2]*axis[2];
    double cross[3] = {
        axis[1]*v[2] - axis[2]*v[1],
        axis[2]*v[0] - axis[0]*v[2],
        axis[0]*v[1] - axis[1]*v[0]
    };
    for (int i = 0; i < 3; i++)
        out[i] = v[i]*c + cross[i]*s + axis[i]*dot*(1.0 - c);
}

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
    struct Option  *opt_instrument, *opt_cam_band;
    struct Flag    *flag_inc, *flag_emi, *flag_pha, *flag_lat;
    struct Flag    *flag_lon, *flag_rad, *flag_res, *flag_all;
    struct Flag    *flag_spice, *flag_camera;
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

    flag_spice = G_define_flag(); flag_spice->key = 's';
    flag_spice->label = _("SPICE mode: real per-pixel ephemeris geometry");
    flag_spice->description = _("Reads target/observer/time and kernel paths from the "
                                 "input map's history (written by p.spiceinit) and computes "
                                 "incidence/emission/phase from real NAIF CSPICE ephemeris "
                                 "(p_spice_ilumin) instead of the flat-field sun_x/y/z, "
                                 "obs_x/y/z vectors. Requires the input map's region to be "
                                 "a real geographic or projected CRS (not an un-georeferenced "
                                 "pixel/line grid, e.g. as produced by 'p.in.pds3 -g' for raw "
                                 "pushbroom cubes) -- see NOTES.");

    flag_camera = G_define_flag(); flag_camera->key = 'c';
    flag_camera->label = _("Camera mode: real per-pixel boresight ray (CRISM only, v1)");
    flag_camera->description = _("Builds a real per-pixel look-direction ray from the "
                                  "instrument's boresight + per-band camera-model "
                                  "coefficients (read from the IK attached via p.spiceinit) "
                                  "and intersects it with the target surface (p_spice_sincpt) "
                                  "instead of assuming the region already gives a known "
                                  "(lon, lat) per pixel. For raw, un-projected pushbroom "
                                  "cubes (e.g. CRISM TRDR/EDR) where -s cannot be used. "
                                  "Requires instrument=. See NOTES.");

    opt_instrument = G_define_option();
    opt_instrument->key         = "instrument";
    opt_instrument->type        = TYPE_STRING;
    opt_instrument->required    = NO;
    opt_instrument->options     = "CRISM_VNIR,CRISM_IR";
    opt_instrument->description = _("Instrument camera model to use with -c (v1: CRISM only)");

    opt_cam_band = G_define_option();
    opt_cam_band->key         = "band";
    opt_cam_band->type        = TYPE_INTEGER;
    opt_cam_band->required    = NO;
    opt_cam_band->description = _("Detector band index for -c's camera-model angle lookup "
                                   "(default: the instrument's own IK reference band, e.g. "
                                   "223 for CRISM VNIR / 247 for CRISM IR -- geometry is "
                                   "computed once at this band for the whole cube, matching "
                                   "the real CRISM DDR convention, not per-band)");

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
    const char *input_mapset = G_find_raster((char *)input, "");
    if (!input_mapset)
        G_fatal_error(_("Raster map <%s> not found"), input);

    /* ---------------------------------------------------------------- */
    /* SPICE mode (-s) setup: real target/observer/time/kernels from      */
    /* history, real ephemeris time, real body radii from the loaded PCK. */
    /* ---------------------------------------------------------------- */
    int spice_mode = flag_spice->answer;
    int camera_mode = flag_camera->answer;
    SpiceHistoryInfo spice_info;
    double et = 0.0;
    char fixref[80] = "";
    char target_upper[64] = "";

    if (spice_mode && camera_mode)
        G_fatal_error(_("-s and -c are mutually exclusive (two different "
                        "ways of getting a per-pixel surface point)."));
    if (camera_mode && !opt_instrument->answer) {
        /* Auto-detect from the sensor= field p.in.archive -s writes into
         * planetary.json for CRISM imports, so -c works without typing
         * instrument= by hand. Falls through to the fatal error below,
         * unchanged, for inputs with no/unrecognized metadata. */
        char sensor_buf[64];
        if (p_meta_read_string_field(input, "raster", "sensor", sensor_buf,
                                      sizeof(sensor_buf)) == 0) {
            if (strcmp(sensor_buf, "MRO_CRISM_VNIR") == 0)
                opt_instrument->answer = "CRISM_VNIR";
            else if (strcmp(sensor_buf, "MRO_CRISM_IR") == 0)
                opt_instrument->answer = "CRISM_IR";
        }
    }
    if (camera_mode && !opt_instrument->answer)
        G_fatal_error(_("-c requires instrument= (v1: CRISM_VNIR or CRISM_IR)."));

    /* Projection handling for -s: a real geographic/projected CRS is
     * required (see NOTES) -- an un-georeferenced pixel/line grid
     * (PROJECTION_XY, e.g. p.in.pds3 -g output for raw pushbroom cubes)
     * cannot be safely interpreted as lat/lon and must fail loudly
     * rather than silently misinterpreting sample/line indices as
     * degrees (the bug this -s mode replaces). */
    int proj_type = 0;
    struct pj_info iproj, oproj, tproj;
    int use_gpj_transform = 0;

    if (spice_mode) {
        p_spice_init();
        spice_info = read_spice_history(input, input_mapset);
        if (p_spice_str2et(spice_info.time, &et) < 0)
            G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                            "time (is an LSK kernel attached?)."), spice_info.time);

        uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
        snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

        double radii[3];
        if (p_spice_radii(spice_info.target, radii) == 0) {
            a_km = radii[0]; b_km = radii[1]; c_km = radii[2];
            G_message(_("SPICE: using real %s radii from loaded PCK: "
                        "a=%.3f b=%.3f c=%.3f km"), target_upper, a_km, b_km, c_km);
        }
        else {
            G_warning(_("SPICE: no PCK radii found for '%s'; falling back to "
                        "a_radius=/b_radius=/c_radius= (%.3f/%.3f/%.3f km)."),
                       spice_info.target, a_km, b_km, c_km);
        }

        proj_type = G_projection();
        if (proj_type == PROJECTION_XY) {
            G_fatal_error(_("SPICE mode (-s) requires a real geographic or "
                            "projected CRS; the current location is an "
                            "un-georeferenced pixel/line grid (PROJECTION_XY, "
                            "e.g. as produced by 'p.in.pds3 -g' for raw "
                            "pushbroom cubes). Re-project to the body's real "
                            "footprint first, or use flat-field mode (no -s)."));
        }
        else if (proj_type != PROJECTION_LL) {
            struct Key_Value *pin = G_get_projinfo();
            struct Key_Value *uin = G_get_projunits();
            if (!pin || !uin)
                G_fatal_error(_("Cannot read projection info/units from the "
                                "current location."));
            if (pj_get_kv(&iproj, pin, uin) < 0)
                G_fatal_error(_("pj_get_kv failed."));
            G_free_key_value(pin);
            G_free_key_value(uin);
            oproj.pj = NULL;
            tproj.def = NULL;
            if (GPJ_init_transform(&iproj, &oproj, &tproj) < 0)
                G_fatal_error(_("GPJ_init_transform failed -- cannot convert "
                                "this location's CRS to geographic lon/lat."));
            use_gpj_transform = 1;
            G_message(_("SPICE: working CRS is projected; converting each "
                        "pixel to real lon/lat via GPJ_transform()."));
        }
        else {
            G_message(_("SPICE: working location is already geographic "
                        "(PROJECTION_LL); using region east/north directly "
                        "as lon/lat."));
        }
    }

    /* ---------------------------------------------------------------- */
    /* Camera mode (-c) setup: real target/observer/time/kernels from     */
    /* history (same as -s), plus the CRISM instrument camera model.      */
    /* No projection/region-CRS handling needed -- row/col are just       */
    /* (line, sample) indices into the camera model, not coordinates.     */
    /* ---------------------------------------------------------------- */
    CrismCameraModel cam;
    const char *camera_method = "Ellipsoid";

    if (camera_mode) {
        p_spice_init();
        spice_info = read_spice_history(input, input_mapset);
        if (p_spice_str2et(spice_info.time, &et) < 0)
            G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                            "time (is an LSK kernel attached?)."), spice_info.time);

        uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
        snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

        double radii[3];
        if (p_spice_radii(spice_info.target, radii) == 0) {
            a_km = radii[0]; b_km = radii[1]; c_km = radii[2];
            G_message(_("SPICE: using real %s radii from loaded PCK: "
                        "a=%.3f b=%.3f c=%.3f km"), target_upper, a_km, b_km, c_km);
        }
        else
            G_warning(_("SPICE: no PCK radii found for '%s'; falling back to "
                        "a_radius=/b_radius=/c_radius= (%.3f/%.3f/%.3f km)."),
                       spice_info.target, a_km, b_km, c_km);

        if (spice_info.have_dsk)
            camera_method = "DSK/Unprioritized";

        load_crism_camera_model(opt_instrument->answer,
                                opt_cam_band->answer ? atoi(opt_cam_band->answer) : 0,
                                opt_cam_band->answer != NULL, &cam);
        G_message(_("Camera mode: instrument=%s frame=%s band=%d "
                    "(a0=%.9f a1=%.9f rad)"),
                   opt_instrument->answer, cam.frame, cam.band, cam.a0, cam.a1);
    }

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
    if (spice_mode || camera_mode) {
        G_message(_("  SPICE: target=%s observer=%s time=%s (et=%.3f)"),
                   target_upper, spice_info.observer, spice_info.time, et);
        if (spice_info.have_line_rate)
            G_message(_("  SPICE: line_rate=%.6f s/row -- per-row ephemeris "
                        "time, not a single mid-scene epoch"),
                       spice_info.line_rate);
    }
    else
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

    int spice_failed_pixels = 0;

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);

        if (spice_mode) {
            /* SPICE mode: each pixel's real lon/lat is already known from
             * the region + CRS (no ray-casting needed -- we're not solving
             * "where does this ray hit", we already know the point), so
             * call p_spice_ilumin() directly per pixel with that known
             * body-fixed surface point and the real ephemeris time.
             *
             * et_row: with line_rate= attached (real per-line cadence),
             * each row gets its own ephemeris time relative to the
             * mid-scene epoch instead of one constant et for the whole
             * scene -- real pushbroom/framing acquisitions take a real,
             * non-zero scan duration, so row 0 and row nrows-1 were not
             * actually acquired at the same instant. Without line_rate=
             * (have_line_rate == 0) this is a no-op (et_row == et). */
            double et_row = et;
            if (spice_info.have_line_rate)
                et_row = et + (row - (nrows - 1) / 2.0) * spice_info.line_rate;

            for (int col = 0; col < ncols; col++) {
                double east  = region.west  + (col + 0.5) * region.ew_res;
                double north = region.north - (row + 0.5) * region.ns_res;

                double lon_deg = east, lat_deg = north;
                if (use_gpj_transform) {
                    if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                      &lon_deg, &lat_deg, NULL) < 0)
                        G_fatal_error(_("GPJ_transform failed at pixel "
                                        "(row=%d, col=%d)."), row, col);
                }
                /* else: PROJECTION_LL -- region east/north already are
                 * real lon/lat, no transform needed. */

                /* DSK kernel attached: use the real (non-ellipsoid) shape
                 * via latsrf -- still no ray-casting/camera model needed,
                 * since we already know this pixel's (lon, lat). Falls
                 * back to the ellipsoid for any (lon, lat) the DSK
                 * doesn't cover (latsrf returns < 0), rather than
                 * G_fatal_error -- a real DSK tile boundary is a normal
                 * condition, not a configuration error. */
                double r_km;
                double pt[3];
                const char *ilumin_method = "Ellipsoid";
                int used_dsk = 0;
                if (spice_info.have_dsk &&
                    p_spice_latsrf("DSK/Unprioritized", target_upper, et_row,
                                  fixref, lon_deg, lat_deg, pt) == 0) {
                    r_km = sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
                    ilumin_method = "DSK/Unprioritized";
                    used_dsk = 1;
                }
                if (!used_dsk) {
                    r_km = p_shape_local_radius_km(shape, lat_deg, lon_deg);
                    p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, pt);
                }

                double phase_deg, incidence_deg, emission_deg;
                int ok = (p_spice_ilumin(ilumin_method, target_upper, et_row, fixref,
                                         "LT+S", spice_info.observer, pt,
                                         &phase_deg, &incidence_deg,
                                         &emission_deg) == 0);

                if (!ok) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                }
                else {
                    if (bp.incidence) row_inc[col] = incidence_deg;
                    if (bp.emission)  row_emi[col] = emission_deg;
                    if (bp.phase)     row_pha[col] = phase_deg;
                }
                if (bp.lat) row_lat[col] = lat_deg;
                if (bp.lon) row_lon[col] = lon_deg;
                if (bp.local_radius) row_rad[col] = r_km;
                if (bp.resolution) {
                    /* Same approximate formula as flat-field mode (v1 --
                     * see p.phocube.md NOTES). */
                    double circ = 2.0 * M_PI * r_km;
                    row_res[col] = circ / ncols * cos(lat_deg * DEG2RAD);
                }
            }
        }
        else if (camera_mode) {
            /* Camera mode: build a real per-pixel boresight ray from the
             * CRISM camera model (cross-track angle = a0 + a1*sample,
             * rotated about the slit direction) and intersect it with
             * the target surface via sincpt -- row/col here are real
             * (line, sample) indices into the raw cube, not coordinates;
             * the surface point (and hence lat/lon) is *found*, not
             * known in advance, unlike -s mode. */
            double et_row = et;
            if (spice_info.have_line_rate)
                et_row = et + (row - (nrows - 1) / 2.0) * spice_info.line_rate;

            for (int col = 0; col < ncols; col++) {
                double theta = cam.a0 + cam.a1 * col;
                double dvec[3];
                rodrigues_rotate(cam.boresight, cam.slit_dir, theta, dvec);

                double pt[3], srfvec[3], trgepc;
                int hit = p_spice_sincpt(camera_method, target_upper, et_row,
                                         fixref, "LT+S", spice_info.observer,
                                         cam.frame, dvec, pt, &trgepc, srfvec);

                if (hit != 1) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                    if (bp.lat)       Rast_set_d_null_value(&row_lat[col], 1);
                    if (bp.lon)       Rast_set_d_null_value(&row_lon[col], 1);
                    if (bp.local_radius) Rast_set_d_null_value(&row_rad[col], 1);
                    if (bp.resolution)   Rast_set_d_null_value(&row_res[col], 1);
                    continue;
                }

                double r_km = sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
                double lon_deg = atan2(pt[1], pt[0]) * RAD2DEG;
                double lat_deg = asin(pt[2] / r_km) * RAD2DEG;

                double phase_deg, incidence_deg, emission_deg;
                int ok = (p_spice_ilumin(camera_method, target_upper, et_row,
                                         fixref, "LT+S", spice_info.observer,
                                         pt, &phase_deg, &incidence_deg,
                                         &emission_deg) == 0);

                if (!ok) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                }
                else {
                    if (bp.incidence) row_inc[col] = incidence_deg;
                    if (bp.emission)  row_emi[col] = emission_deg;
                    if (bp.phase)     row_pha[col] = phase_deg;
                }
                if (bp.lat) row_lat[col] = lat_deg;
                if (bp.lon) row_lon[col] = lon_deg;
                if (bp.local_radius) row_rad[col] = r_km;
                if (bp.resolution) {
                    double circ = 2.0 * M_PI * r_km;
                    row_res[col] = circ / ncols * cos(lat_deg * DEG2RAD);
                }
            }
        }
        else {
            /* Flat-field mode: compute look direction from observer to
             * pixel. We derive pixel lat/lon from the GRASS region, then
             * compute the body-fixed XYZ point on the ellipsoid surface
             * at that lat/lon and form the look direction obs→point. */
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

    if ((spice_mode || camera_mode) && spice_failed_pixels > 0)
        G_warning(_("SPICE: %d/%d pixels failed (ray missed the body, or "
                    "ilumin/sincpt error) -- set NULL in output bands."),
                   spice_failed_pixels, nrows * ncols);
    if (spice_mode || camera_mode)
        p_spice_clear();

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
