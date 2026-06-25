/****************************************************************************
 *
 * MODULE:       p.caminfo
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Report real SPICE camera geometry for a raw, p.spiceinit'd
 *               planetary camera image: centre/corner lat/lon, illumination
 *               at centre, sub-solar/sub-spacecraft points, solar distance,
 *               pixel ground resolution, and north azimuth.
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
#include <ctype.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "../../libs/p_shapemodel/p_shapemodel.h"
#include "../../libs/p_spice/p_spice.h"
#include "../../libs/p_meta/p_meta.h"

#define AU_KM 149597870.7
#define RAD2DEG (180.0 / M_PI)

/* ------------------------------------------------------------------ */
/* Camera model: CRISM (1-D pushbroom + time-varying gimbal CK) and    */
/* Cassini ISS (2-D framing + K1 radial distortion), the exact same    */
/* pinhole model p.phocube -c and p.cam2map -c already use -- see      */
/* those modules for OMEGA (whiskbroom mirror) and VIMS (2-axis scan), */
/* deliberately not duplicated here yet (see TODO.md).                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int    naif_id;
    char   frame[64];
    double boresight_sample;
    double boresight_line;
    double pixel_pitch;
    double focal_length;
    double k1;
    int    is_framing;  /* 1: ISS (2-D, dy is a real focal-plane offset).
                          * 0: CRISM (1-D, dy stays 0 -- per-line pointing
                          * comes from the gimbal CK over time instead). */
} PinholeCameraModel;

static void load_pinhole_camera_model(const char *instrument,
                                       const char *input_map,
                                       const char *filter1, const char *filter2,
                                       int image_cols, int image_rows,
                                       PinholeCameraModel *cam)
{
    memset(cam, 0, sizeof(*cam));
    int is_iss = 0;

    if (strcmp(instrument, "CRISM_VNIR") == 0) {
        cam->naif_id = -74017;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_VNIR");
    }
    else if (strcmp(instrument, "CRISM_IR") == 0) {
        cam->naif_id = -74018;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_IR");
    }
    else if (strcmp(instrument, "ISS_NAC") == 0) {
        cam->naif_id = -82360;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_NAC_USGS");
        is_iss = 1;
        cam->is_framing = 1;
    }
    else if (strcmp(instrument, "ISS_WAC") == 0) {
        cam->naif_id = -82361;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_WAC_USGS");
        is_iss = 1;
        cam->is_framing = 1;
    }
    else
        G_fatal_error(_("Unsupported instrument='%s' -- only CRISM_VNIR, "
                        "CRISM_IR, ISS_NAC, ISS_WAC are implemented. MEX "
                        "OMEGA (whiskbroom mirror) and Cassini VIMS (2-axis "
                        "scan) need extra per-pixel inputs not yet wired "
                        "into p.caminfo -- see p.phocube -c and TODO.md."),
                       instrument);

    char varname[80];
    int n;

    snprintf(varname, sizeof(varname), "INS%d_PIXEL_PITCH", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->pixel_pitch) < 0 || n != 1)
        G_fatal_error(_("Could not read %s from the loaded IK -- has the "
                        "instrument addendum kernel (IAK) been attached via "
                        "p.spiceinit's ik=, in addition to the regular IK?"),
                       varname);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_SAMPLE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_sample) < 0 || n != 1)
        G_fatal_error(_("Could not read %s from the loaded IK."), varname);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_LINE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_line) < 0 || n != 1)
        G_fatal_error(_("Could not read %s from the loaded IK."), varname);

    if (!is_iss) {
        snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
            G_fatal_error(_("Could not read %s from the loaded IK."), varname);
        return;
    }

    /* ISS: real radial distortion, always present. */
    snprintf(varname, sizeof(varname), "INS%d_K1", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->k1) < 0 || n != 1)
        G_fatal_error(_("Could not read %s from the loaded IK."), varname);

    /* BORESIGHT_SAMPLE/LINE and PIXEL_PITCH in the IK/IAK are given in the
     * full-resolution (1x1) detector frame, but real images are often
     * acquired SUMMED/binned (e.g. INSTRUMENT_MODE_ID="SUM2" -> half
     * resolution) -- detect the summing factor by comparing the IK's own
     * full-frame INS<id>_PIXEL_SAMPLES/PIXEL_LINES to this image's actual
     * dimensions (same fix verified in p.phocube -c and p.cam2map -c,
     * see TODO.md candidate #5). */
    double pixel_samples_full = 0.0, pixel_lines_full = 0.0;
    snprintf(varname, sizeof(varname), "INS%d_PIXEL_SAMPLES", cam->naif_id);
    int have_ps = (p_spice_gdpool_d(varname, 0, 1, &n, &pixel_samples_full) == 0 && n == 1);
    snprintf(varname, sizeof(varname), "INS%d_PIXEL_LINES", cam->naif_id);
    int have_pl = (p_spice_gdpool_d(varname, 0, 1, &n, &pixel_lines_full) == 0 && n == 1);

    double summing_s = 1.0, summing_l = 1.0;
    if (have_ps && image_cols > 0)
        summing_s = pixel_samples_full / (double)image_cols;
    if (have_pl && image_rows > 0)
        summing_l = pixel_lines_full / (double)image_rows;

    if (summing_s != 1.0 || summing_l != 1.0) {
        G_message(_("Detected summing/binning factor %.0fx%.0f (IK full "
                    "frame %.0fx%.0f vs image %dx%d) -- adjusting "
                    "boresight and pixel pitch accordingly."),
                   summing_s, summing_l, pixel_samples_full, pixel_lines_full,
                   image_cols, image_rows);
        cam->boresight_sample = (cam->boresight_sample - 1.0) / summing_s + 1.0;
        cam->boresight_line   = (cam->boresight_line   - 1.0) / summing_l + 1.0;
    }
    cam->pixel_pitch *= (summing_s + summing_l) / 2.0;

    /* Focal length varies per filter pair -- resolve filter1/filter2 from
     * -c's own options, else the raster's p_meta sidecar, else fall back
     * to the IAK's own DEFAULT_FOCAL_LENGTH (documented fallback, same as
     * p.phocube's ISS path). */
    char f1[32] = "", f2[32] = "";
    if (filter1) snprintf(f1, sizeof(f1), "%s", filter1);
    if (filter2) snprintf(f2, sizeof(f2), "%s", filter2);

    if (!f1[0] || !f2[0]) {
        char filt_buf[64];
        if (p_meta_read_string_field(input_map, "raster", "filter_name",
                                      filt_buf, sizeof(filt_buf)) == 0) {
            char *slash = strchr(filt_buf, '/');
            if (slash) {
                *slash = '\0';
                snprintf(f1, sizeof(f1), "%s", filt_buf);
                snprintf(f2, sizeof(f2), "%s", slash + 1);
            }
        }
    }

    int got_focal = 0;
    if (f1[0] && f2[0]) {
        snprintf(varname, sizeof(varname), "INS%d_%s_%s_FOCAL_LENGTH",
                 cam->naif_id, f1, f2);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) == 0 && n == 1)
            got_focal = 1;
        else
            G_warning(_("%s not found in the loaded IAK (wrong filter "
                        "order? real keys are label order, e.g. CL1_CL2 "
                        "not CL2_CL1) -- falling back to "
                        "DEFAULT_FOCAL_LENGTH."), varname);
    }
    if (!got_focal) {
        snprintf(varname, sizeof(varname), "INS%d_DEFAULT_FOCAL_LENGTH", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
            G_fatal_error(_("No usable FOCAL_LENGTH found (no filter1=/"
                            "filter2=, no 'filter_name' in the raster's "
                            "planetary.json, and %s missing too)."),
                           varname);
        if (!f1[0] || !f2[0])
            G_warning(_("No filter1=/filter2= given and no 'filter_name' "
                        "in this raster's planetary.json -- using %s."),
                       varname);
    }
}

/* SPICE history metadata, written by p.spiceinit -- mirrors p.phocube's
 * own read_spice_history() (not shared via a library, see that module). */
typedef struct {
    char target[64];
    char observer[64];
    char time[64];
    double line_rate;
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
        if (strncmp(line, "SPICE_", 6) != 0)
            continue;
        const char *kv = line + 6;
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
        G_fatal_error(_("Requires target=, observer= and time= to have "
                        "been attached to <%s> via p.spiceinit (found: "
                        "target=%s observer=%s time=%s)."),
                       mapname,
                       info.have_target ? info.target : "(missing)",
                       info.have_observer ? info.observer : "(missing)",
                       info.have_time ? info.time : "(missing)");
    if (info.n_kernels_loaded == 0)
        G_fatal_error(_("Found no loadable kernels in <%s>'s history -- "
                        "run p.spiceinit with lsk=/spk=/etc first."),
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

/* A point in the camera-mode sample/line grid, evaluated to a body-fixed
 * surface point (if the look-direction ray hits the target). */
typedef struct {
    int    hit;
    double lat_deg, lon_deg, radius_km;
    double spoint[3];
} CamPoint;

static CamPoint eval_pixel(const PinholeCameraModel *cam,
                            const char *camera_method,
                            const char *target, const char *fixref,
                            const char *observer,
                            double et, double line_rate, int have_line_rate,
                            int nrows, int row, int col)
{
    CamPoint result;
    memset(&result, 0, sizeof(result));

    double et_row = et;
    if (have_line_rate)
        et_row = et + (row - (nrows - 1) / 2.0) * line_rate;

    double sample_1based = col + 1.0;
    double line_1based   = row + 1.0;
    double dx = (sample_1based - cam->boresight_sample) * cam->pixel_pitch;
    double dy = cam->is_framing
                  ? (line_1based - cam->boresight_line) * cam->pixel_pitch
                  : 0.0;

    double r2 = dx * dx + dy * dy;
    double ux = dx * (1.0 + cam->k1 * r2);
    double uy = dy * (1.0 + cam->k1 * r2);
    double dvec[3] = { ux, uy, cam->focal_length };

    double srfvec[3], trgepc;
    int hit = p_spice_sincpt(camera_method, target, et_row, fixref, "LT+S",
                             observer, cam->frame, dvec, result.spoint,
                             &trgepc, srfvec);
    if (hit != 1)
        return result;

    result.hit = 1;
    p_shape_xyz_to_latlon(result.spoint, &result.lat_deg, &result.lon_deg,
                          &result.radius_km);
    return result;
}

/* Clockwise angle (degrees, 0-360) from image "up" (decreasing line) to
 * true north, at a given body-fixed surface point. Approximates the local
 * outward normal as spoint/|spoint| (exact for a sphere; a small
 * approximation for a flattened ellipsoid, in keeping with this project's
 * other documented-approximation formulas -- see p.phocube.md NOTES).
 * Correctly accounts for the same fixref/camera-frame light-time epoch
 * split as p.cam2map -c's inverse rotation (see TODO.md candidate #5):
 * the body-fixed point is only meaningful at the surface epoch trgepc
 * returned by sincpt, not the observer's epoch et. */
static int north_azimuth_deg(const PinholeCameraModel *cam,
                              const char *fixref, const double spoint[3],
                              double trgepc, double et, double *azimuth_deg)
{
    double r = sqrt(spoint[0]*spoint[0] + spoint[1]*spoint[1] + spoint[2]*spoint[2]);
    if (r < 1.0e-9)
        return -1;
    double n_hat[3] = { spoint[0]/r, spoint[1]/r, spoint[2]/r };
    double north_pole[3] = { 0.0, 0.0, 1.0 };
    double dot = north_pole[0]*n_hat[0] + north_pole[1]*n_hat[1] + north_pole[2]*n_hat[2];
    double north_local[3] = { north_pole[0] - dot*n_hat[0],
                              north_pole[1] - dot*n_hat[1],
                              north_pole[2] - dot*n_hat[2] };
    double nl = sqrt(north_local[0]*north_local[0] + north_local[1]*north_local[1] +
                     north_local[2]*north_local[2]);
    if (nl < 1.0e-9)
        return -1;  /* at the pole -- azimuth undefined */
    for (int i = 0; i < 3; i++) north_local[i] /= nl;

    double rot1[3][3], rot2[3][3];
    if (p_spice_pxform(fixref, "J2000", trgepc, rot1) < 0 ||
        p_spice_pxform("J2000", cam->frame, et, rot2) < 0)
        return -1;

    double inertial[3], cam_vec[3];
    for (int i = 0; i < 3; i++)
        inertial[i] = rot1[i][0]*north_local[0] + rot1[i][1]*north_local[1] +
                      rot1[i][2]*north_local[2];
    for (int i = 0; i < 3; i++)
        cam_vec[i] = rot2[i][0]*inertial[0] + rot2[i][1]*inertial[1] +
                     rot2[i][2]*inertial[2];

    double az = atan2(cam_vec[0], -cam_vec[1]) * RAD2DEG;
    if (az < 0.0) az += 360.0;
    *azimuth_deg = az;
    return 0;
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_instrument, *opt_filter1, *opt_filter2;
    struct Flag    *flag_json;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Camera & Geometry"));
    G_add_keyword(_("camera"));
    G_add_keyword(_("SPICE"));
    G_add_keyword(_("geometry"));
    module->label       = _("Report real SPICE camera geometry for a planetary image.");
    module->description = _("For a raw camera image with kernels attached via "
                             "p.spiceinit, computes and prints: centre/corner "
                             "lat/lon, illumination (incidence/emission/phase) "
                             "at centre, sub-solar and sub-spacecraft points, "
                             "solar distance, pixel ground resolution, and "
                             "north azimuth -- all from real per-pixel camera "
                             "rays via the loaded SPICE kernels, the same "
                             "camera model p.phocube -c and p.cam2map -c use.");

    opt_input = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->key = "input";

    opt_output = G_define_option();
    opt_output->key         = "output";
    opt_output->type        = TYPE_STRING;
    opt_output->required    = NO;
    opt_output->description = _("Output JSON file path (optional)");

    opt_instrument = G_define_option();
    opt_instrument->key      = "instrument";
    opt_instrument->type     = TYPE_STRING;
    opt_instrument->required = YES;
    opt_instrument->options  = "CRISM_VNIR,CRISM_IR,ISS_NAC,ISS_WAC";
    opt_instrument->description = _("Instrument camera model to use");

    opt_filter1 = G_define_option();
    opt_filter1->key         = "filter1";
    opt_filter1->type        = TYPE_STRING;
    opt_filter1->required    = NO;
    opt_filter1->description = _("ISS_NAC/ISS_WAC: first filter wheel position "
                                  "(else from the raster's planetary.json, "
                                  "else DEFAULT_FOCAL_LENGTH)");

    opt_filter2 = G_define_option();
    opt_filter2->key         = "filter2";
    opt_filter2->type        = TYPE_STRING;
    opt_filter2->required    = NO;
    opt_filter2->description = _("ISS_NAC/ISS_WAC: second filter wheel position");

    flag_json = G_define_flag();
    flag_json->key = 'j';
    flag_json->description = _("Print output in JSON format");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *input_mapset = G_find_raster(opt_input->answer, "");
    if (!input_mapset)
        G_fatal_error(_("Raster map <%s> not found"), opt_input->answer);

    struct Cell_head in_region;
    Rast_get_cellhd(opt_input->answer, input_mapset, &in_region);
    int nrows = in_region.rows, ncols = in_region.cols;

    p_spice_init();
    SpiceHistoryInfo spice_info = read_spice_history(opt_input->answer, input_mapset);

    double et;
    if (p_spice_str2et(spice_info.time, &et) < 0)
        G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                        "time (is an LSK kernel attached?)."), spice_info.time);

    char target_upper[64], fixref[80];
    uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
    snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

    const char *camera_method = spice_info.have_dsk ? "DSK/Unprioritized" : "Ellipsoid";

    PinholeCameraModel cam;
    load_pinhole_camera_model(opt_instrument->answer, opt_input->answer,
                               opt_filter1->answer, opt_filter2->answer,
                               ncols, nrows, &cam);

    G_message(_("instrument=%s frame=%s target=%s observer=%s time=%s "
                "(et=%.6f) image=%dx%d"), opt_instrument->answer, cam.frame,
               target_upper, spice_info.observer, spice_info.time, et,
               ncols, nrows);

    int row_c = nrows / 2, col_c = ncols / 2;
    CamPoint centre = eval_pixel(&cam, camera_method, target_upper, fixref,
                                 spice_info.observer, et, spice_info.line_rate,
                                 spice_info.have_line_rate, nrows, row_c, col_c);

    struct { const char *name; int row, col; } corner_defs[4] = {
        { "corner_ul", 0,         0 },
        { "corner_ur", 0,         ncols - 1 },
        { "corner_ll", nrows - 1, 0 },
        { "corner_lr", nrows - 1, ncols - 1 },
    };
    CamPoint corners[4];
    for (int i = 0; i < 4; i++)
        corners[i] = eval_pixel(&cam, camera_method, target_upper, fixref,
                                spice_info.observer, et, spice_info.line_rate,
                                spice_info.have_line_rate, nrows,
                                corner_defs[i].row, corner_defs[i].col);

    double phase_deg = 0, incidence_deg = 0, emission_deg = 0;
    int have_illum = 0;
    if (centre.hit) {
        have_illum = (p_spice_ilumin(camera_method, target_upper, et, fixref,
                                     "LT+S", spice_info.observer, centre.spoint,
                                     &phase_deg, &incidence_deg, &emission_deg) == 0);
    }

    double subslr_pt[3], subslr_trgepc, subslr_srfvec[3];
    double subslr_lat = 0, subslr_lon = 0, subslr_r;
    int have_subslr = (p_spice_subslr(camera_method, target_upper, et, fixref,
                                      "LT+S", spice_info.observer, subslr_pt,
                                      &subslr_trgepc, subslr_srfvec) == 0);
    if (have_subslr)
        p_shape_xyz_to_latlon(subslr_pt, &subslr_lat, &subslr_lon, &subslr_r);

    double subpnt_pt[3], subpnt_trgepc, subpnt_srfvec[3];
    double subpnt_lat = 0, subpnt_lon = 0, subpnt_r;
    int have_subpnt = (p_spice_subpnt(camera_method, target_upper, et, fixref,
                                      "LT+S", spice_info.observer, subpnt_pt,
                                      &subpnt_trgepc, subpnt_srfvec) == 0);
    if (have_subpnt)
        p_shape_xyz_to_latlon(subpnt_pt, &subpnt_lat, &subpnt_lon, &subpnt_r);

    double solar_distance_au = 0;
    int have_solar_distance = 0;
    {
        double sun_pos[3], sun_lt;
        if (p_spice_pos(target_upper, et, "J2000", "LT+S", "SUN", sun_pos, &sun_lt) == 0) {
            double d = sqrt(sun_pos[0]*sun_pos[0] + sun_pos[1]*sun_pos[1] +
                            sun_pos[2]*sun_pos[2]);
            solar_distance_au = d / AU_KM;
            have_solar_distance = 1;
        }
    }

    double resolution_m = 0;
    int have_resolution = 0;
    double az_deg = 0;
    int have_az = 0;
    if (centre.hit) {
        double obs_pos[3], obs_lt;
        if (p_spice_pos(target_upper, et, fixref, "LT+S", spice_info.observer,
                        obs_pos, &obs_lt) == 0) {
            double ray[3] = { centre.spoint[0] + obs_pos[0],
                              centre.spoint[1] + obs_pos[1],
                              centre.spoint[2] + obs_pos[2] };
            double range_km = sqrt(ray[0]*ray[0] + ray[1]*ray[1] + ray[2]*ray[2]);
            double ifov_rad = cam.pixel_pitch / cam.focal_length;
            resolution_m = ifov_rad * range_km * 1000.0;
            have_resolution = 1;

            /* sincpt doesn't hand back trgepc for the centre point above
             * (only hit/spoint were kept) -- recompute it here via the
             * same ray, cheap and avoids widening CamPoint for one field. */
            double et_row_c = et;
            if (spice_info.have_line_rate)
                et_row_c = et + (row_c - (nrows - 1) / 2.0) * spice_info.line_rate;
            double sample_1based = col_c + 1.0, line_1based = row_c + 1.0;
            double dx = (sample_1based - cam.boresight_sample) * cam.pixel_pitch;
            double dy = cam.is_framing
                          ? (line_1based - cam.boresight_line) * cam.pixel_pitch : 0.0;
            double r2 = dx*dx + dy*dy;
            double dvec[3] = { dx*(1.0+cam.k1*r2), dy*(1.0+cam.k1*r2), cam.focal_length };
            double spoint2[3], srfvec2[3], trgepc;
            if (p_spice_sincpt(camera_method, target_upper, et_row_c, fixref,
                               "LT+S", spice_info.observer, cam.frame, dvec,
                               spoint2, &trgepc, srfvec2) == 1) {
                have_az = (north_azimuth_deg(&cam, fixref, spoint2, trgepc,
                                             et_row_c, &az_deg) == 0);
            }
        }
    }

    if (flag_json->answer) {
        printf("{\n");
        printf("  \"input\": \"%s\",\n", opt_input->answer);
        printf("  \"instrument\": \"%s\",\n", opt_instrument->answer);
        printf("  \"target\": \"%s\",\n", target_upper);
        printf("  \"observer\": \"%s\",\n", spice_info.observer);
        printf("  \"time\": \"%s\",\n", spice_info.time);
        printf("  \"centre_hit\": %s,\n", centre.hit ? "true" : "false");
        if (centre.hit) {
            printf("  \"centre_lat_deg\": %.6f,\n", centre.lat_deg);
            printf("  \"centre_lon_deg\": %.6f,\n", centre.lon_deg);
        }
        if (have_illum) {
            printf("  \"incidence_deg\": %.6f,\n", incidence_deg);
            printf("  \"emission_deg\": %.6f,\n", emission_deg);
            printf("  \"phase_deg\": %.6f,\n", phase_deg);
        }
        for (int i = 0; i < 4; i++) {
            printf("  \"%s_hit\": %s,\n", corner_defs[i].name,
                   corners[i].hit ? "true" : "false");
            if (corners[i].hit) {
                printf("  \"%s_lat_deg\": %.6f,\n", corner_defs[i].name, corners[i].lat_deg);
                printf("  \"%s_lon_deg\": %.6f,\n", corner_defs[i].name, corners[i].lon_deg);
            }
        }
        if (have_subslr) {
            printf("  \"subsolar_lat_deg\": %.6f,\n", subslr_lat);
            printf("  \"subsolar_lon_deg\": %.6f,\n", subslr_lon);
        }
        if (have_subpnt) {
            printf("  \"subspacecraft_lat_deg\": %.6f,\n", subpnt_lat);
            printf("  \"subspacecraft_lon_deg\": %.6f,\n", subpnt_lon);
        }
        if (have_solar_distance)
            printf("  \"solar_distance_au\": %.6f,\n", solar_distance_au);
        if (have_resolution)
            printf("  \"pixel_resolution_m\": %.6f,\n", resolution_m);
        if (have_az)
            printf("  \"north_azimuth_deg\": %.6f,\n", az_deg);
        printf("  \"image_rows\": %d,\n", nrows);
        printf("  \"image_cols\": %d\n", ncols);
        printf("}\n");
    }
    else {
        fprintf(stdout, "Input:                %s\n", opt_input->answer);
        fprintf(stdout, "Instrument:           %s\n", opt_instrument->answer);
        fprintf(stdout, "Target/Observer/Time: %s / %s / %s\n",
                target_upper, spice_info.observer, spice_info.time);
        if (centre.hit)
            fprintf(stdout, "Centre lat/lon:       %.6f / %.6f deg\n",
                    centre.lat_deg, centre.lon_deg);
        else
            fprintf(stdout, "Centre lat/lon:       no intercept\n");
        if (have_illum)
            fprintf(stdout, "Incidence/Emission/Phase (centre): %.4f / %.4f / %.4f deg\n",
                    incidence_deg, emission_deg, phase_deg);
        for (int i = 0; i < 4; i++) {
            if (corners[i].hit)
                fprintf(stdout, "%-10s lat/lon:    %.6f / %.6f deg\n",
                        corner_defs[i].name, corners[i].lat_deg, corners[i].lon_deg);
            else
                fprintf(stdout, "%-10s lat/lon:    no intercept\n", corner_defs[i].name);
        }
        if (have_subslr)
            fprintf(stdout, "Sub-solar lat/lon:    %.6f / %.6f deg\n", subslr_lat, subslr_lon);
        if (have_subpnt)
            fprintf(stdout, "Sub-spacecraft lat/lon: %.6f / %.6f deg\n", subpnt_lat, subpnt_lon);
        if (have_solar_distance)
            fprintf(stdout, "Solar distance:       %.6f AU\n", solar_distance_au);
        if (have_resolution)
            fprintf(stdout, "Pixel resolution:     %.3f m/pixel (at centre)\n", resolution_m);
        if (have_az)
            fprintf(stdout, "North azimuth:        %.3f deg (at centre)\n", az_deg);
        fprintf(stdout, "Dimensions:           %d rows x %d cols\n", nrows, ncols);
    }

    if (opt_output->answer) {
        FILE *fp = fopen(opt_output->answer, "w");
        if (fp) {
            fprintf(fp, "{\n  \"input\":\"%s\",\"instrument\":\"%s\","
                    "\"centre_lat\":%.6f,\"centre_lon\":%.6f,"
                    "\"incidence_deg\":%.6f,\"emission_deg\":%.6f,\"phase_deg\":%.6f,"
                    "\"solar_distance_au\":%.6f,\"pixel_resolution_m\":%.6f,"
                    "\"north_azimuth_deg\":%.6f\n}\n",
                    opt_input->answer, opt_instrument->answer,
                    centre.lat_deg, centre.lon_deg,
                    incidence_deg, emission_deg, phase_deg,
                    solar_distance_au, resolution_m, az_deg);
            fclose(fp);
            G_message(_("Geometry info written to: %s"), opt_output->answer);
        }
    }

    return EXIT_SUCCESS;
}
