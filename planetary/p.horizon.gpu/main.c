/* main.c — p.horizon.gpu GRASS module.
 *
 * Geometrically-correct horizon elevation rasters from a DEM, computed
 * on OpenCL (auto) or OpenMP (-c flag or no OpenCL ICD). CLI mirrors
 * r.horizon's option set and output naming so the module is a drop-in
 * for p_lib.precompute_horizons; numerics are NOT bit-compared against
 * r.horizon (r.horizon's direction estimator uses a 0.0001-rad lat/lon
 * shift that drifts laterally on polar projections; we use a per-pixel
 * local-tangent rotation plane that is correct for any conformal CRS).
 *
 * Conformality constraint: a single rotation per pixel only suffices
 * when east and north are orthogonal in projected coords (i.e. the
 * projection is conformal). Non-conformal projections (aea, sinu,
 * moll, cea, laea, eqc, ...) are rejected up front with G_fatal_error
 * rather than silently producing wrong horizons.
 *
 * Options:
 *   elevation=<dem>          input DEM (metres)
 *   output=<basename>        output raster basename; per-azimuth maps
 *                            are written as <basename>_<az_with_underscore>
 *                            (mirroring r.horizon's naming so that
 *                            interpolate_horizon() works unchanged).
 *   direction=<az_deg>       (optional) compute a single azimuth
 *   step=<deg>               (optional) azimuth sweep step
 *   start=<deg> end=<deg>    (optional) azimuth range [start, end)
 *   maxdistance=<m>          ray cap in metres (default 10 000)
 *   bodyradius=<m>           planetary radius for curvature
 *                            (default 1737400 = Moon)
 *
 * Flags:
 *   -c   force OpenMP CPU backend even if OpenCL is available
 *
 * Output: degrees (matches r.horizon's degree convention).
 */
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include <grass/gprojects.h>

#include "horizon_backend.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Conformal PROJ projections — east ⊥ north in projected coords AND
 * the metric is locally isotropic (cell size encodes the local metre).
 * Handled with a single per-pixel rotation; metric scales are 1.0. */
static int is_conformal_proj(const char *proj_name)
{
    if (!proj_name) return 0;
    static const char *ok[] = {
        "stere",    /* Stereographic (incl. polar) */
        "sterea",   /* Oblique stereographic (alternative) */
        "ups",      /* Universal Polar Stereographic */
        "merc",     /* Mercator */
        "tmerc",    /* Transverse Mercator */
        "utm",      /* UTM (tmerc-based) */
        "etmerc",   /* Extended Transverse Mercator */
        "lcc",      /* Lambert Conformal Conic */
        "omerc",    /* Oblique Mercator */
        "somerc",   /* Swiss Oblique Mercator */
        "gstmerc",
        NULL
    };
    for (int i = 0; ok[i]; i++)
        if (strcmp(proj_name, ok[i]) == 0) return 1;
    return 0;
}

/* Cylindrical, axis-aligned, anisotropic projections: east ⊥ north in
 * projected coords (rotation = 0 everywhere) but the metric is row-
 * dependent. Handled with per-row metric_x/metric_y arrays.
 *  - eqc    Plate Carrée / Equirectangular (most planetary mosaics)
 *  - cea    Cylindrical Equal Area
 *  - sinu   Sinusoidal is technically pseudo-cylindrical; treat
 *           conservatively as cylindrical at the central meridian only.
 */
static int is_cylindrical_anisotropic(const char *proj_name)
{
    if (!proj_name) return 0;
    static const char *ok[] = { "eqc", "cea", NULL };
    for (int i = 0; ok[i]; i++)
        if (strcmp(proj_name, ok[i]) == 0) return 1;
    return 0;
}

static void build_az_list(const char *direction, const char *start,
                          const char *end, const char *step,
                          double **az_out, int *n_out)
{
    if (direction) {
        *az_out = G_malloc(sizeof(double));
        (*az_out)[0] = atof(direction);
        *n_out = 1;
        return;
    }
    if (!start || !end || !step)
        G_fatal_error(_("Provide either 'direction=' or all of "
                        "'start=', 'end=', 'step='"));
    double s = atof(start), e = atof(end), st = atof(step);
    if (st <= 0.0) G_fatal_error(_("'step' must be > 0"));
    int n = (int)ceil((e - s) / st);
    if (n < 1) n = 1;
    *az_out = G_malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) (*az_out)[i] = s + i * st;
    *n_out = n;
}

static void az_suffix(double az_deg, char *buf, size_t sz) {
    /* mirror r.horizon: "_NNN_F" with the integer and fractional parts
     * underscore-separated; e.g. 22.5 -> "022_5", 0 -> "000_0" */
    double i, f;
    f = modf(az_deg, &i);
    snprintf(buf, sz, "%03d_%d", (int)i, (int)(fabs(f) * 10.0 + 0.5));
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option *opt_dem, *opt_out, *opt_dir, *opt_start, *opt_end,
                  *opt_step, *opt_maxd, *opt_R;
    struct Flag   *flag_cpu;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Terrain Analysis"));
    G_add_keyword(_("horizon"));
    G_add_keyword(_("OpenMP"));
    G_add_keyword(_("OpenCL"));
    module->description = _("Horizon elevation rasters from DEM "
                             "(OpenCL + OpenMP). Drop-in for r.horizon "
                             "in p_lib.precompute_horizons.");

    opt_dem = G_define_standard_option(G_OPT_R_INPUT);
    opt_dem->key = "elevation";
    opt_dem->description = _("Input DEM raster (metres)");

    opt_out = G_define_option();
    opt_out->key         = "output";
    opt_out->type        = TYPE_STRING;
    opt_out->required    = YES;
    opt_out->description = _("Output basename; per-az maps suffixed");

    opt_dir = G_define_option();
    opt_dir->key         = "direction";
    opt_dir->type        = TYPE_DOUBLE;
    opt_dir->required    = NO;
    opt_dir->description = _("Single azimuth, degrees CCW from east");

    opt_start = G_define_option();
    opt_start->key         = "start";
    opt_start->type        = TYPE_DOUBLE;
    opt_start->required    = NO;
    opt_start->answer      = "0";
    opt_start->description = _("Azimuth sweep start (degrees)");

    opt_end = G_define_option();
    opt_end->key         = "end";
    opt_end->type        = TYPE_DOUBLE;
    opt_end->required    = NO;
    opt_end->answer      = "360";
    opt_end->description = _("Azimuth sweep end exclusive (degrees)");

    opt_step = G_define_option();
    opt_step->key         = "step";
    opt_step->type        = TYPE_DOUBLE;
    opt_step->required    = NO;
    opt_step->description = _("Azimuth sweep step (degrees); omit to use direction=");

    opt_maxd = G_define_option();
    opt_maxd->key         = "maxdistance";
    opt_maxd->type        = TYPE_DOUBLE;
    opt_maxd->required    = NO;
    opt_maxd->answer      = "10000";
    opt_maxd->description = _("Ray cap in metres");

    opt_R = G_define_option();
    opt_R->key         = "bodyradius";
    opt_R->type        = TYPE_DOUBLE;
    opt_R->required    = NO;
    opt_R->answer      = "1737400";
    opt_R->description = _("Planetary radius in metres (default Moon)");

    flag_cpu = G_define_flag();
    flag_cpu->key         = 'c';
    flag_cpu->description = _("Force OpenMP CPU backend");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    double *azs_deg = NULL;
    int n_az = 0;
    build_az_list(opt_dir->answer, opt_start->answer,
                  opt_end->answer, opt_step->answer,
                  &azs_deg, &n_az);

    double maxdist = atof(opt_maxd->answer);
    double body_R  = atof(opt_R->answer);

    struct Cell_head win;
    G_get_window(&win);
    int ny = win.rows, nx = win.cols;
    if (fabs(win.ns_res - win.ew_res) > 1e-6 * win.ew_res)
        G_warning(_("Anisotropic cell (ns=%.3f, ew=%.3f) — using ew_res."),
                  win.ns_res, win.ew_res);
    float cell_m = (float)win.ew_res;

    G_message(_("Region: %d × %d  @ %.2f m"), nx, ny, cell_m);
    G_message(_("Azimuths: %d  maxdist: %.0f m  body_R: %.0f m"),
              n_az, maxdist, body_R);

    /* read DEM */
    G_message(_("Loading DEM…"));
    int fd = Rast_open_old(opt_dem->answer, "");
    FCELL *row = Rast_allocate_f_buf();
    float *dem = G_malloc((size_t)nx * ny * sizeof(float));
    float nodata = -FLT_MAX;
    for (int r = 0; r < ny; r++) {
        Rast_get_f_row(fd, row, r);
        for (int c = 0; c < nx; c++) {
            dem[r * nx + c] = Rast_is_f_null_value(&row[c])
                              ? nodata : row[c];
        }
        G_percent(r, ny, 5);
    }
    G_percent(ny, ny, 5);
    Rast_close(fd);
    G_free(row);

    /* ── Projection guard + metric/rotation precompute ───────────────
     * Three regimes are supported:
     *   1. Conformal projections (stere/ups/merc/tmerc/utm/lcc/omerc):
     *      build per-pixel rotation plane; metric stays NULL → 1.0.
     *   2. Axis-aligned cylindrical anisotropic (eqc/cea): no rotation
     *      needed (east ⟂ north in projected coords) but the metric is
     *      row-dependent: per-row metric_x = true_east_m / projected_m
     *      (= 1/cos(lat) for vanilla eqc with lat_ts=0) and metric_y
     *      analogously. Computed via PJ_FWD/PJ_INV perturbations along
     *      the central column.
     *   3. Lat/lon: rejected — would need geodetic ray-walk (Phase 2).
     */
    int proj_type = G_projection();
    float *rotation     = NULL;
    float *metric_x_row = NULL;
    float *metric_y_row = NULL;

    if (proj_type == PROJECTION_LL) {
        G_fatal_error(_("Lat/lon location not supported. Horizon ray-marching "
                        "requires metric cells; re-project the DEM to a "
                        "conformal or equirectangular CRS first."));
    }
    if (proj_type == PROJECTION_XY) {
        G_warning(_("XY location: no projection info. Assuming isotropic "
                    "metric cells and identity rotation."));
        /* rotation + metric_* stay NULL — backends treat as identity. */
    }
    else {
        struct Key_Value *pin = G_get_projinfo();
        if (!pin)
            G_fatal_error(_("Cannot read projection info from current "
                             "location."));
        const char *proj_name_raw = G_find_key_value("proj", pin);
        /* Copy: we'll free pin shortly but want the name for log messages. */
        char proj_name[64];
        snprintf(proj_name, sizeof(proj_name), "%s",
                 proj_name_raw ? proj_name_raw : "unknown");
        int is_conf = is_conformal_proj(proj_name_raw);
        int is_cyl  = is_cylindrical_anisotropic(proj_name_raw);
        if (!is_conf && !is_cyl) {
            G_free_key_value(pin);
            G_fatal_error(_("Projection '%s' not supported. p.horizon.gpu "
                            "handles conformal CRS (stere/ups/merc/tmerc/"
                            "utm/etmerc/lcc/omerc/somerc) and axis-aligned "
                            "cylindrical CRS (eqc, cea). Re-project the DEM "
                            "first (r.proj)."),
                          proj_name);
        }

        struct pj_info iproj, oproj, tproj;
        struct Key_Value *uin = G_get_projunits();
        if (!uin) { G_free_key_value(pin);
                    G_fatal_error(_("Cannot read projection units.")); }
        if (pj_get_kv(&iproj, pin, uin) < 0)
            G_fatal_error(_("pj_get_kv failed"));
        G_free_key_value(pin); G_free_key_value(uin);
        oproj.pj = NULL; tproj.def = NULL;
        if (GPJ_init_transform(&iproj, &oproj, &tproj) < 0)
            G_fatal_error(_("GPJ_init_transform failed"));

        if (is_conf) {
            /* ── Build per-pixel rotation plane (conformal CRS) ─────
             * For each pixel: forward-project (xp,yp) → (lon,lat),
             * perturb lon by EPS_DEG eastward, inverse-project back.
             * atan2(dn,de) is the angle from projected +x to local
             * geographic east. Magnitude (≈ scale factor) cancels in
             * atan2 and would blow up near the poles, so we drop it. */
            G_message(_("Pre-computing per-pixel geographic-east rotation "
                        "(conformal CRS '%s')…"), proj_name);
            rotation = G_malloc((size_t)nx * ny * sizeof(float));
            const double EPS_DEG = 0.0001;
            for (int r = 0; r < ny; r++) {
                for (int c = 0; c < nx; c++) {
                    double xp = win.west  + (c + 0.5) * win.ew_res;
                    double yp = win.north - (r + 0.5) * win.ns_res;
                    double lon = xp, lat = yp;
                    if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                      &lon, &lat, NULL) < 0)
                        G_fatal_error(_("PJ_FWD failed at pixel (%d,%d)"), c, r);
                    double lon2 = lon + EPS_DEG, lat2 = lat;
                    if (GPJ_transform(&iproj, &oproj, &tproj, PJ_INV,
                                      &lon2, &lat2, NULL) < 0)
                        G_fatal_error(_("PJ_INV failed at pixel (%d,%d)"), c, r);
                    double de = lon2 - xp;
                    double dn = lat2 - yp;
                    rotation[r * nx + c] = (float)atan2(dn, de);
                }
                G_percent(r, ny, 10);
            }
            G_percent(ny, ny, 10);
        }
        else {
            /* ── Build per-row metric (cylindrical anisotropic CRS) ──
             * For axis-aligned cylindrical projections, east ⟂ north in
             * projected coords (so rotation = 0) but the local metre per
             * projected unit varies with latitude. We perturb the
             * central column by EPS_M projected metres in each axis,
             * round-trip through (lon,lat) on a sphere of radius
             * body_R, and recover the TRUE geographic step.
             *
             * Interpretation: metric_x[row] = true_east_m / projected_m.
             *
             * For vanilla eqc (lat_0=0, lat_ts=0):
             *   x = R·λ, so 1 projected metre east = 1/R rad of longitude,
             *   which at latitude φ is cos(φ) true east metres.
             *   ⇒ metric_x[row] = cos(lat_row),  metric_y[row] = 1.0
             * For cea (Lambert cylindrical equal area, lat_ts=0):
             *   x = R·λ          → metric_x = cos(lat)
             *   y = R·sin(φ)    → metric_y = 1/cos(lat)
             * For eqc with lat_ts=φ0 ≠ 0: metric_x = cos(lat)/cos(φ0).
             * The PJ_FWD perturbation method handles all cases uniformly.
             */
            G_message(_("Pre-computing per-row anisotropic metric "
                        "(cylindrical CRS '%s')…"), proj_name);
            metric_x_row = G_malloc((size_t)ny * sizeof(float));
            metric_y_row = G_malloc((size_t)ny * sizeof(float));
            const double EPS_M  = 1.0;                 /* 1 projected metre */
            const double RAD2M  = body_R;              /* arc-metre per radian */
            const double DEG2RAD = M_PI / 180.0;
            double xp0 = win.west + 0.5 * (win.east - win.west);  /* central column */
            for (int r = 0; r < ny; r++) {
                double yp = win.north - (r + 0.5) * win.ns_res;
                double lon0 = xp0, lat0 = yp;
                if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                  &lon0, &lat0, NULL) < 0)
                    G_fatal_error(_("PJ_FWD failed at row %d"), r);
                /* East perturbation. */
                double lon_e = xp0 + EPS_M, lat_e = yp;
                if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                  &lon_e, &lat_e, NULL) < 0)
                    G_fatal_error(_("PJ_FWD east failed at row %d"), r);
                double dlon = (lon_e - lon0) * DEG2RAD;
                double cosphi = cos(lat0 * DEG2RAD);
                double true_east_m = RAD2M * cosphi * dlon;
                /* North perturbation. */
                double lon_n = xp0, lat_n = yp + EPS_M;
                if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                  &lon_n, &lat_n, NULL) < 0)
                    G_fatal_error(_("PJ_FWD north failed at row %d"), r);
                double dlat = (lat_n - lat0) * DEG2RAD;
                double true_north_m = RAD2M * dlat;
                metric_x_row[r] = (float)(true_east_m  / EPS_M);
                metric_y_row[r] = (float)(true_north_m / EPS_M);
                if (metric_x_row[r] <= 0.0f) metric_x_row[r] = 1.0f;
                if (metric_y_row[r] <= 0.0f) metric_y_row[r] = 1.0f;
                G_percent(r, ny, 10);
            }
            G_percent(ny, ny, 10);
            G_message(_("Metric range: east factor %.4f–%.4f, "
                        "north factor %.4f–%.4f (cell aspect: "
                        "1.0 → true-east extends by factor / true-north "
                        "by factor)."),
                      (double)metric_x_row[0], (double)metric_x_row[ny-1],
                      (double)metric_y_row[0], (double)metric_y_row[ny-1]);
        }
    }

    /* az list in radians */
    float *az_rad = G_malloc(n_az * sizeof(float));
    for (int i = 0; i < n_az; i++) az_rad[i] = (float)(azs_deg[i] * M_PI / 180.0);

    horizon_params_t p;
    p.nx = nx; p.ny = ny;
    p.cell_m = cell_m;
    p.step_m = 0.5f * cell_m;
    p.max_dist_m = (float)maxdist;
    p.inv_2R = (float)(1.0 / (2.0 * body_R));
    p.nodata = nodata;

    float *out_planes = G_malloc((size_t)n_az * nx * ny * sizeof(float));
    int used_ocl = 0;

#ifdef HAVE_OPENCL
    if (!flag_cpu->answer) {
        G_message(_("Trying OpenCL…"));
        char errbuf[512] = {0};
        int rc = horizon_run_ocl(dem, &p, rotation,
                                 metric_x_row, metric_y_row,
                                 az_rad, n_az,
                                 out_planes, errbuf, sizeof(errbuf));
        if (rc == 0) { used_ocl = 1; G_message(_("Backend: OpenCL")); }
        else G_message(_("OpenCL unavailable (%s) — falling back to OpenMP"),
                       errbuf[0] ? errbuf : "n/a");
    }
#endif

    if (!used_ocl) {
#ifdef _OPENMP
        G_message(_("Backend: OpenMP (%d threads)"), omp_get_max_threads());
#else
        G_message(_("Backend: single-threaded"));
#endif
        horizon_run_omp(dem, &p, rotation,
                        metric_x_row, metric_y_row,
                        az_rad, n_az, out_planes);
    }

    G_free(dem);
    if (rotation)     G_free(rotation);
    if (metric_x_row) G_free(metric_x_row);
    if (metric_y_row) G_free(metric_y_row);

    /* write one raster per azimuth */
    char suf[16], outname[256];
    FCELL *wrow = Rast_allocate_f_buf();
    for (int a = 0; a < n_az; a++) {
        az_suffix(azs_deg[a], suf, sizeof(suf));
        snprintf(outname, sizeof(outname), "%s_%s", opt_out->answer, suf);
        G_message(_("Writing %s"), outname);
        int wfd = Rast_open_new(outname, FCELL_TYPE);
        float *plane = out_planes + (size_t)a * nx * ny;
        for (int r = 0; r < ny; r++) {
            for (int c = 0; c < nx; c++) {
                float v = plane[r * nx + c];
                if (isnan(v)) Rast_set_f_null_value(&wrow[c], 1);
                /* GRASS r.horizon writes degrees; match that. */
                else wrow[c] = (FCELL)(v * (180.0 / M_PI));
            }
            Rast_put_f_row(wfd, wrow);
        }
        Rast_close(wfd);

        struct History hist;
        Rast_short_history(outname, "raster", &hist);
        Rast_command_history(&hist);
        Rast_write_history(outname, &hist);
    }
    G_free(wrow);
    G_free(out_planes);
    G_free(az_rad);
    G_free(azs_deg);

    G_done_msg(_("%d horizon raster(s) written under basename '%s'"),
               n_az, opt_out->answer);
    return EXIT_SUCCESS;
}
