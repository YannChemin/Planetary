/****************************************************************************
 *
 * MODULE:       p.crater.draw
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Automated crater-rim delineation from a DEM and/or a
 *               visual (panchromatic) image, producing a GRASS vector
 *               of polygon rims that feeds directly into p.crater
 *               (scaling) and p.crater.freq (size-frequency dating).
 *
 *               v1 (Phase P1+P2) ships two detectors:
 *                 - DEM-based   (rim/floor radial-profile analysis)
 *                 - Image-based (sun-azimuth-dependent shadow pairs)
 *               with optional combined-mode using non-max suppression
 *               across detector outputs. ML-based detectors (Strategy
 *               C/D in the design doc) are deferred to later phases.
 *
 *               Diameter range capped at 100 m .. 10 km in v1.
 *               See manual FUTURE WORK for multi-ring basin detection
 *               (concentric-circle aggregation) and large-basin scaling.
 *
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 *               See https://unlicense.org and the LICENSE file.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include <grass/raster.h>
#include "p_crater_draw.h"
#include "detect_ml.h"
#include "opencl_runtime.h"
#include "refine.h"

/* ---- Minimal body -> d/D lookup (mirrors p.crater::planet_db). ----- */
static double dD_default_for_body(const char *name)
{
    if (!name) return 0.196;
    /* Rock-like bodies */
    if (!strcasecmp(name, "moon"))    return 0.196;
    if (!strcasecmp(name, "mars"))    return 0.150;
    if (!strcasecmp(name, "mercury")) return 0.180;
    if (!strcasecmp(name, "venus"))   return 0.140;
    if (!strcasecmp(name, "earth"))   return 0.130;
    if (!strcasecmp(name, "vesta"))   return 0.180;
    if (!strcasecmp(name, "ceres"))   return 0.170;
    if (!strcasecmp(name, "io"))      return 0.150;
    /* Icy moons / TNOs (alphabetical, all share 0.150 per the database) */
    static const char *icy[] = {
        "europa","ganymede","callisto","titan","mimas","enceladus","tethys",
        "dione","rhea","iapetus","phoebe","miranda","ariel","umbriel",
        "titania","oberon","triton","pluto","charon","eris","haumea",
        "makemake","gonggong","quaoar","sedna","orcus","salacia"
    };
    for (size_t i = 0; i < sizeof(icy)/sizeof(icy[0]); i++)
        if (!strcasecmp(name, icy[i])) return 0.150;
    /* Hyperion is porous water ice but treated as 0.180 in p.crater. */
    if (!strcasecmp(name, "hyperion")) return 0.180;
    /* Rubble piles / small bodies */
    if (!strcasecmp(name, "phobos") || !strcasecmp(name, "deimos") ||
        !strcasecmp(name, "pallas") || !strcasecmp(name, "hygiea") ||
        !strcasecmp(name, "psyche") || !strcasecmp(name, "lutetia") ||
        !strcasecmp(name, "mathilde")|| !strcasecmp(name, "eros") ||
        !strcasecmp(name, "itokawa")|| !strcasecmp(name, "bennu") ||
        !strcasecmp(name, "ryugu") || !strcasecmp(name, "67p"))
        return 0.200;
    /* Unknown -> safe fallback. */
    return 0.196;
}

static int sample_raster_at_xy(int fd, double x, double y, double *out)
{
    struct Cell_head w; Rast_get_window(&w);
    int row = (int)Rast_northing_to_row(y, &w);
    int col = (int)Rast_easting_to_col (x, &w);
    if (row < 0 || row >= w.rows || col < 0 || col >= w.cols) return 0;
    DCELL *buf = Rast_allocate_d_buf();
    Rast_get_d_row(fd, buf, row);
    int ok = !Rast_is_d_null_value(&buf[col]);
    if (ok) *out = buf[col];
    G_free(buf);
    return ok;
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_dem, *opt_image, *opt_sun_az;
    struct Option  *opt_min_d, *opt_max_d, *opt_threshold;
    struct Option  *opt_method, *opt_n_scales, *opt_n_az;
    struct Option  *opt_body, *opt_output, *opt_iou;
    struct Option  *opt_nthreads;
    struct Option  *opt_dd_simple, *opt_dd_map;
    struct Option  *opt_basin_ctr, *opt_basin_ratio;
    struct Option  *opt_ml_model;
    struct Flag    *flag_multiring;
    struct Flag    *flag_opencl;
    struct Flag    *flag_norefine;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Crater Analysis"));
    G_add_keyword(_("crater"));
    G_add_keyword(_("detection"));
    G_add_keyword(_("vector"));
    G_add_keyword(_("draw"));
    module->label = _("Automated crater-rim delineation from DEM and/or "
                       "panchromatic image");
    module->description =
        _("Scans a DEM (rim/floor radial-profile detector) and/or a "
          "panchromatic image (sun-azimuth-dependent shadow-pair "
          "detector), emits a vector of polygon rims with attributes "
          "(D_eq, confidence, method) directly consumable by p.crater "
          "(input=) and p.crater.freq (vector= column=D_eq). "
          "Default diameter range 100 m – 10 km; raise max_diameter= freely.");

    opt_dem = G_define_standard_option(G_OPT_R_INPUT);
    opt_dem->key         = "dem";
    opt_dem->required    = NO;
    opt_dem->description = _("Input DEM raster (metric CRS). Required for "
                              "method=dem or method=both.");

    opt_image = G_define_standard_option(G_OPT_R_INPUT);
    opt_image->key         = "image";
    opt_image->required    = NO;
    opt_image->description = _("Input panchromatic image raster. Required "
                                 "for method=image or method=both.");

    opt_sun_az = G_define_option();
    opt_sun_az->key         = "sun_azimuth";
    opt_sun_az->type        = TYPE_DOUBLE;
    opt_sun_az->required    = NO;
    opt_sun_az->description = _("Sun azimuth [deg, 0=N, clockwise] at "
                                  "image acquisition. Required when image= "
                                  "is given.");

    opt_min_d = G_define_option();
    opt_min_d->key         = "min_diameter";
    opt_min_d->type        = TYPE_DOUBLE;
    opt_min_d->required    = NO;
    opt_min_d->answer      = "100";
    opt_min_d->description = _("Minimum crater diameter to search for [m]");

    opt_max_d = G_define_option();
    opt_max_d->key         = "max_diameter";
    opt_max_d->type        = TYPE_DOUBLE;
    opt_max_d->required    = NO;
    opt_max_d->answer      = "10000";
    opt_max_d->description = _("Maximum crater diameter to search for [m]. "
                                 "Default 10 km; raise freely - run time "
                                 "scales with (max/min)^2.");

    opt_threshold = G_define_option();
    opt_threshold->key         = "threshold";
    opt_threshold->type        = TYPE_DOUBLE;
    opt_threshold->required    = NO;
    opt_threshold->answer      = "0.55";
    opt_threshold->description = _("Detection confidence threshold (0..1)");

    opt_method = G_define_option();
    opt_method->key         = "method";
    opt_method->type        = TYPE_STRING;
    opt_method->required    = NO;
    opt_method->answer      = "auto";
    opt_method->options     = "auto,dem,image,both,ml";
    opt_method->description = _("Detector chain. 'auto' picks based on "
                                  "supplied inputs. 'ml' runs P1+P2 then "
                                  "rescores via the shallow-ML head "
                                  "(P3) loaded from ml_model= or a "
                                  "uniform-weight baseline.");

    opt_iou = G_define_option();
    opt_iou->key         = "nms_iou";
    opt_iou->type        = TYPE_DOUBLE;
    opt_iou->required    = NO;
    opt_iou->answer      = "0.30";
    opt_iou->description = _("Non-max-suppression IoU threshold");

    opt_n_scales = G_define_option();
    opt_n_scales->key         = "scales";
    opt_n_scales->type        = TYPE_INTEGER;
    opt_n_scales->required    = NO;
    opt_n_scales->answer      = "8";
    opt_n_scales->description = _("Number of log-spaced diameter scales");

    opt_n_az = G_define_option();
    opt_n_az->key         = "azimuth_samples";
    opt_n_az->type        = TYPE_INTEGER;
    opt_n_az->required    = NO;
    opt_n_az->answer      = "16";
    opt_n_az->description = _("Rim samples per circle (DEM detector)");

    opt_body = G_define_option();
    opt_body->key         = "body";
    opt_body->type        = TYPE_STRING;
    opt_body->required    = NO;
    opt_body->answer      = "moon";
    opt_body->description = _("Planetary body (logged only in v1; future "
                                "versions may tune scales per body)");

    opt_nthreads = G_define_option();
    opt_nthreads->key         = "threads";
    opt_nthreads->type        = TYPE_INTEGER;
    opt_nthreads->required    = NO;
    opt_nthreads->answer      = "0";
    opt_nthreads->description = _("Number of OpenMP threads (0 = library "
                                    "default)");

    opt_output = G_define_standard_option(G_OPT_V_OUTPUT);
    opt_output->description = _("Output vector map of detected crater rims");

    opt_dd_simple = G_define_option();
    opt_dd_simple->key         = "dd_simple";
    opt_dd_simple->type        = TYPE_DOUBLE;
    opt_dd_simple->required    = NO;
    opt_dd_simple->description = _("Bake a uniform simple-crater d/D "
                                     "ratio (0..0.5) into every detected "
                                     "polygon. Highest priority after "
                                     "dd_simple_map=.");

    opt_dd_map = G_define_standard_option(G_OPT_R_INPUT);
    opt_dd_map->key         = "dd_simple_map";
    opt_dd_map->required    = NO;
    opt_dd_map->description = _("Per-pixel simple-crater d/D raster; "
                                  "sampled at each detected centroid "
                                  "and baked into the output polygon's "
                                  "dD_simple attribute. Overrides "
                                  "dd_simple= scalar and body default.");

    flag_opencl = G_define_flag();
    flag_opencl->key         = 'c';
    flag_opencl->description = _("Try OpenCL acceleration (falls back to "
                                   "OpenMP if no device available). v1 has "
                                   "OpenCL stubbed out - OpenMP is used.");

    flag_norefine = G_define_flag();
    flag_norefine->key         = 'R';
    flag_norefine->description = _("Skip sub-pixel centre/radius refinement "
                                     "(refinement is ON by default and needs "
                                     "dem=; it sharpens DEM/merged centres so "
                                     "concentric rings of one basin group "
                                     "under -m).");

    flag_multiring = G_define_flag();
    flag_multiring->key         = 'm';
    flag_multiring->description = _("After NMS, aggregate concentric "
                                      "detections as multi-ring basins "
                                      "(adds basin_id and ring_index "
                                      "columns to the output).");

    opt_basin_ctr = G_define_option();
    opt_basin_ctr->key         = "basin_centre_tol";
    opt_basin_ctr->type        = TYPE_DOUBLE;
    opt_basin_ctr->required    = NO;
    opt_basin_ctr->answer      = "0.10";
    opt_basin_ctr->description = _("Multi-ring basin: max centre offset "
                                     "as a fraction of the smaller radius "
                                     "(default 0.10, i.e. 10%)");

    opt_basin_ratio = G_define_option();
    opt_basin_ratio->key         = "basin_ring_ratio";
    opt_basin_ratio->type        = TYPE_DOUBLE;
    opt_basin_ratio->required    = NO;
    opt_basin_ratio->answer      = "1.30";
    opt_basin_ratio->description = _("Multi-ring basin: minimum r_outer/"
                                       "r_inner ratio between adjacent "
                                       "rings (default 1.30, i.e. 30% "
                                       "size step)");

    opt_ml_model = G_define_option();
    opt_ml_model->key         = "ml_model";
    opt_ml_model->type        = TYPE_STRING;
    opt_ml_model->required    = NO;
    opt_ml_model->description = _("Path to a shallow-ML model binary "
                                    "(.bin). When method=ml and this is "
                                    "unset, a uniform-weight baseline "
                                    "is applied. See manual ML PHASE "
                                    "(P3) section for the file format.");
    opt_ml_model->gisprompt   = "old_file,file,input";

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* ---- Resolve method chain ---- */
    int want_dem = 0, want_image = 0, want_ml = 0;
    if (strcmp(opt_method->answer, "auto") == 0) {
        want_dem   = opt_dem  ->answer != NULL;
        want_image = opt_image->answer != NULL;
    } else if (strcmp(opt_method->answer, "dem")   == 0) want_dem = 1;
    else  if (strcmp(opt_method->answer, "image") == 0) want_image = 1;
    else if (strcmp(opt_method->answer, "ml") == 0) {
        /* P3 is the meta-detector. It accepts EITHER P1 alone, P2
         * alone, OR both - whichever inputs are supplied. The
         * detect_ml.c::make_features() routine fills the missing
         * channel with 0 so a single-detector run still yields a
         * valid feature vector.                                       */
        want_dem   = opt_dem  ->answer != NULL;
        want_image = opt_image->answer != NULL;
        want_ml    = 1;
        if (!want_dem && !want_image)
            G_fatal_error(_("method=ml requires at least one of "
                              "dem= or image="));
    }
    else { want_dem = 1; want_image = 1; }

    if (want_dem && !opt_dem->answer)
        G_fatal_error(_("method=%s requires dem= raster"),
                      opt_method->answer);
    if (want_image) {
        if (!opt_image->answer)
            G_fatal_error(_("method=%s requires image= raster"),
                          opt_method->answer);
        if (!opt_sun_az->answer)
            G_fatal_error(_("image= mode requires sun_azimuth= "
                              "(degrees, 0=N, clockwise)"));
    }
    if (!want_dem && !want_image)
        G_fatal_error(_("No detector enabled - supply dem= and/or image= "
                         "and a compatible method="));

    /* ---- Common configuration ---- */
    DrawConfig cfg;
    cfg.d_min        = atof(opt_min_d->answer);
    cfg.d_max        = atof(opt_max_d->answer);
    if (cfg.d_max > 10000.0)
        G_message(_("max_diameter=%.0f m exceeds the default 10 km; "
                     "performance scales with (d_max/d_min)^2 - "
                     "increase scales= and expect longer run times."),
                  cfg.d_max);
    cfg.threshold    = atof(opt_threshold->answer);
    cfg.n_scales     = atoi(opt_n_scales->answer);
    cfg.n_az_samples = atoi(opt_n_az->answer);
    cfg.n_threads    = atoi(opt_nthreads->answer);
    cfg.use_opencl   = flag_opencl->answer ? 1 : 0;
    cfg.body         = opt_body->answer;

    if (cfg.use_opencl) {
        if (p_crater_draw_opencl_available(1)) {
            G_message(_("OpenCL device available (%s) - GPU kernels "
                         "for the detector inner loops are not wired in "
                         "v1; falling back to OpenMP."),
                      p_crater_draw_opencl_describe());
        } else {
            G_message(_("OpenCL requested but no device found (%s); "
                         "running on OpenMP CPU path."),
                      p_crater_draw_opencl_describe());
        }
    }

    G_message(_("Detector chain: %s%s%s   body=%s   "
                 "diameter [%.0f, %.0f] m   threshold=%.2f"),
              want_dem ? "DEM " : "",
              (want_dem && want_image) ? "+ " : "",
              want_image ? "IMAGE" : "",
              cfg.body, cfg.d_min, cfg.d_max, cfg.threshold);

    /* ---- Run detectors ---- */
    CandidateList all;
    cl_init(&all);

    if (want_dem) {
        if (detect_dem(opt_dem->answer, &cfg, &all) != 0)
            G_fatal_error(_("DEM detector failed"));
    }
    if (want_image) {
        double sun_az = atof(opt_sun_az->answer);
        if (detect_image(opt_image->answer, sun_az, &cfg, &all) != 0)
            G_fatal_error(_("Image detector failed"));
    }

    /* ---- ML rescoring (P3) before NMS ---- */
    if (want_ml) {
        MLModel model = ml_load_model(opt_ml_model->answer);
        CandidateList ml_out;
        cl_init(&ml_out);
        detect_ml_rescore(&all, &model, &ml_out);
        cl_free(&all);
        all = ml_out;
    }

    /* ---- NMS ---- */
    apply_nms(&all, atof(opt_iou->answer));

    /* ---- Sub-pixel centre/radius refinement (DEM/merged only) ----
     * Runs between NMS and multi-ring aggregation: the coarse detector
     * strides at r/3, so its centres are too quantised for concentric
     * rings of one basin to land on a shared point. Needs a DEM; image-
     * only candidates are untouched. ON unless -R given.                */
    if (!flag_norefine->answer && opt_dem->answer && all.n > 0) {
        if (refine_candidates_dem(&all, opt_dem->answer,
                                  cfg.n_az_samples) < 0)
            G_warning(_("Sub-pixel refinement could not load dem=%s; "
                         "leaving coarse centres unchanged."),
                      opt_dem->answer);
    }

    /* ---- Optional multi-ring basin aggregation ---- */
    if (flag_multiring->answer) {
        aggregate_multiring(&all,
                              atof(opt_basin_ctr->answer),
                              atof(opt_basin_ratio->answer));
    } else {
        /* Make sure the columns are zero-initialised so SQL writes NULL. */
        for (int i = 0; i < all.n; i++) {
            all.data[i].basin_id   = 0;
            all.data[i].ring_index = 0;
        }
    }

    /* ---- Bake per-polygon d/D into the survivors ----
     * Priority: dd_simple_map raster sample > dd_simple scalar > body
     * default. A value of 0.0 in the CraterCandidate signals "not set",
     * which writes NULL to the table - downstream p.crater then falls
     * back to its own override chain.                                  */
    int fd_dd = -1;
    if (opt_dd_map->answer)
        fd_dd = Rast_open_old(opt_dd_map->answer, "");
    double dd_scalar = 0.0;
    if (opt_dd_simple->answer) {
        dd_scalar = atof(opt_dd_simple->answer);
        if (dd_scalar <= 0.0 || dd_scalar > 0.5)
            G_fatal_error(_("dd_simple=%.3f out of (0, 0.5]"), dd_scalar);
    }
    double dd_body = dD_default_for_body(cfg.body);
    G_message(_("d/D bake: body default = %.3f%s%s"),
              dd_body,
              dd_scalar > 0.0 ? ", scalar override active" : "",
              fd_dd >= 0     ? ", raster override active" : "");

    for (int i = 0; i < all.n; i++) {
        double dd = 0.0;
        if (fd_dd >= 0) {
            double v;
            if (sample_raster_at_xy(fd_dd, all.data[i].cx,
                                      all.data[i].cy, &v)
                && v > 0.0 && v <= 0.5)
                dd = v;
        }
        if (dd <= 0.0 && dd_scalar > 0.0) dd = dd_scalar;
        if (dd <= 0.0)                    dd = dd_body;
        all.data[i].dD_simple = dd;
    }
    if (fd_dd >= 0) Rast_close(fd_dd);

    write_candidates_vector(opt_output->answer, &all);
    cl_free(&all);
    return EXIT_SUCCESS;
}
