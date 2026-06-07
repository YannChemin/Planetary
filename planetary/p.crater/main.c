/****************************************************************************
 *
 * MODULE:       p.crater
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Improved impact-crater scaling module that merges and
 *               extends grass-addons r.crater (Melosh 1989 ch.7 equations)
 *               with:
 *                 1. Built-in planetary body database (gravity, densities,
 *                    target type) for 16 Solar System bodies (Moon, Mars,
 *                    Mercury, Venus, Earth, Ceres, Vesta, Europa, ...)
 *                 2. Vector input of crater rim polygons - per-feature
 *                    evaluation, equivalent-circle diameter computed
 *                    from polygon area (D_eq = 2 * sqrt(A / pi))
 *                 3. Optional surface and sub-surface geology raster
 *                    inputs (target_type, target_density) - sampled at
 *                    crater centroid for per-crater material overrides
 *                 4. Optional DEM input - depth measured from rim/centre
 *                    elevation difference for cross-validation against
 *                    diameter-based estimates
 *                 5. Multi-method evaluation: Pi-scaling, Gault scaling,
 *                    Yield scaling all computed and stored as separate
 *                    attribute columns (Dat_pi, Dat_gault, Dat_yield,
 *                    proj_pi, proj_gault, proj_yield, kinetic_J, tnt_kt,
 *                    Df, depth_pred, depth_dem, dD_ratio)
 *
 *               Both directions are supported:
 *                 - Default (forward): given an impactor size, compute
 *                   transient + final crater diameter and depth
 *                 - Backward (-b flag): given a measured crater rim,
 *                   compute the projectile diameter that produced it
 *
 *               Equations from:
 *                 Melosh, H. J. (1989). Impact Cratering: A Geologic
 *                   Process. Oxford Univ. Press. ISBN 0-19-504284-0.
 *                 Holsapple, K. A. (1993). The Scaling of Impact
 *                   Processes in Planetary Sciences. Ann. Rev. Earth
 *                   Planet. Sci. 21, 333-373.
 *                 Pike, R. J. (1980). Control of crater morphology by
 *                   gravity and target type: Mars, Earth, Moon. LPSC 11.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               This software is released into the public domain.
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
#include <grass/vector.h>
#include <grass/dbmi.h>
#include <grass/glocale.h>

#include "planet_db.h"
#include "crater_eq.h"
#include "impactor_points.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Sample a raster value at given map (x,y).                            */
/* Returns 1 on success, 0 if cell is NULL or out of region.            */
/* ------------------------------------------------------------------ */
static int sample_raster(int fd, double x, double y, double *value)
{
    struct Cell_head window;
    Rast_get_window(&window);
    int row = (int)Rast_northing_to_row(y, &window);
    int col = (int)Rast_easting_to_col(x, &window);
    if (row < 0 || row >= window.rows || col < 0 || col >= window.cols)
        return 0;
    DCELL *buf = Rast_allocate_d_buf();
    Rast_get_d_row(fd, buf, row);
    int ok = !Rast_is_d_null_value(&buf[col]);
    if (ok) *value = buf[col];
    G_free(buf);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Add a numeric column (DOUBLE PRECISION) to an existing table.        */
/* Silently succeeds if column already exists.                          */
/* ------------------------------------------------------------------ */
static void add_column(dbDriver *driver, const char *table, const char *col)
{
    char sql[512];
    dbString stmt;
    db_init_string(&stmt);
    snprintf(sql, sizeof(sql),
             "ALTER TABLE %s ADD COLUMN %s DOUBLE PRECISION", table, col);
    db_set_string(&stmt, sql);
    /* Errors (e.g. column exists) are non-fatal. */
    db_execute_immediate(driver, &stmt);
    db_free_string(&stmt);
}

/* ------------------------------------------------------------------ */
/* UPDATE one numeric value for one cat.                                */
/* ------------------------------------------------------------------ */
static void update_value(dbDriver *driver, const char *table,
                          const char *key_col, int cat,
                          const char *col, double value)
{
    char sql[512];
    dbString stmt;
    db_init_string(&stmt);
    if (isnan(value) || isinf(value)) {
        snprintf(sql, sizeof(sql),
                 "UPDATE %s SET %s = NULL WHERE %s = %d",
                 table, col, key_col, cat);
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE %s SET %s = %.6g WHERE %s = %d",
                 table, col, value, key_col, cat);
    }
    db_set_string(&stmt, sql);
    if (db_execute_immediate(driver, &stmt) != DB_OK)
        G_warning(_("Failed to update %s for cat %d"), col, cat);
    db_free_string(&stmt);
}

/* ------------------------------------------------------------------ */
/* Depth-weighted effective target density for a two-layer geology.     */
/*                                                                      */
/* Excavation depth is approximated as d_exc = Dat / 3 (Melosh 1989     */
/* §5.4). The effective density is the linear depth-weighted average    */
/* over the column actually excavated:                                  */
/*                                                                      */
/*   if d_exc <= h_surf:       rho_eff = rho_surf                       */
/*   if d_exc <= h_surf+h_sub: rho_eff = (h_surf*rho_surf +             */
/*                                          (d_exc - h_surf)*rho_sub)   */
/*                                         / d_exc                      */
/*   else (excavation deeper than both layers): treat as fully mixed,   */
/*       rho_eff = (h_surf*rho_surf + h_sub*rho_sub)/(h_surf+h_sub)     */
/* ------------------------------------------------------------------ */
static double effective_density(double Dat,
                                 double rho_surf, double h_surf,
                                 double rho_sub,  double h_sub)
{
    if (h_surf <= 0.0 || rho_sub <= 0.0)
        return rho_surf;
    double d_exc = Dat / 3.0;
    if (d_exc <= h_surf)
        return rho_surf;
    if (h_sub > 0.0 && d_exc <= h_surf + h_sub) {
        return (h_surf * rho_surf + (d_exc - h_surf) * rho_sub) / d_exc;
    }
    /* h_sub == 0 means "infinite" subsurface layer */
    if (h_sub <= 0.0) {
        return (h_surf * rho_surf + (d_exc - h_surf) * rho_sub) / d_exc;
    }
    return (h_surf * rho_surf + h_sub * rho_sub) / (h_surf + h_sub);
}

/* ------------------------------------------------------------------ */
/* Compute planar area [m^2] of a closed polygon via the shoelace       */
/* formula. Assumes projected coordinates (metres).                     */
/* ------------------------------------------------------------------ */
static double polygon_area_m2(const struct line_pnts *Pts)
{
    if (!Pts || Pts->n_points < 3) return 0.0;
    double A = 0.0;
    for (int i = 0; i < Pts->n_points - 1; i++) {
        A += Pts->x[i] * Pts->y[i + 1] - Pts->x[i + 1] * Pts->y[i];
    }
    return 0.5 * fabs(A);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_layer, *opt_body;
    struct Option  *opt_vel, *opt_angle, *opt_proj_density;
    struct Option  *opt_proj_diam;
    struct Option  *opt_g_override, *opt_targ_density_override;
    struct Option  *opt_targ_type_override, *opt_dd_simple;
    struct Option  *opt_geol_density, *opt_geol_type, *opt_dem;
    struct Option  *opt_surf_thick, *opt_sub_density, *opt_sub_thick;
    struct Option  *opt_dd_map;
    struct Option  *opt_impactors;
    struct Option  *opt_col_v, *opt_col_a, *opt_col_az;
    struct Option  *opt_col_rho, *opt_col_L, *opt_dens_unit;
    struct Flag    *flag_backward, *flag_overwrite_attrs;

    G_gisinit(argv[0]);

    /* ------------------------------------------------------------ */
    /* Module metadata                                                */
    /* ------------------------------------------------------------ */
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Crater Analysis"));
    G_add_keyword(_("vector"));
    G_add_keyword(_("crater"));
    G_add_keyword(_("impact"));
    G_add_keyword(_("scaling"));
    module->label = _("Impact crater scaling on planetary surfaces.");
    module->description =
        _("For each crater rim polygon in an input vector map, computes "
          "Pi-, Gault- and Yield-scaling estimates of the apparent "
          "transient diameter (forward mode, default) or the projectile "
          "diameter that produced the observed crater (-b backward mode). "
          "Built-in planetary body database supplies surface gravity, "
          "bulk density and target type, overridable via options. "
          "Optional surface-geology raster and DEM enable per-crater "
          "material variability and direct depth measurement. "
          "All computed quantities are written to the output vector "
          "attribute table.");

    /* ---- Vector input: EITHER existing rims (default scaling mode)
     * OR a point map of impactors (synthesis mode, forward only). ---- */
    opt_input = G_define_standard_option(G_OPT_V_INPUT);
    opt_input->required    = NO;
    opt_input->description = _("Input vector of crater rim polygons "
                                 "(scaling mode). Mutually exclusive with "
                                 "impactors=.");

    opt_impactors = G_define_standard_option(G_OPT_V_INPUT);
    opt_impactors->key         = "impactors";
    opt_impactors->required    = NO;
    opt_impactors->description = _("Input vector of impactor POINTS - "
                                     "switches the module to forward "
                                     "synthesis mode. Requires dem= and "
                                     "the five attribute columns named by "
                                     "col_velocity= / col_angle= / "
                                     "col_azimuth= / col_density= / "
                                     "col_diameter=.");

    opt_layer = G_define_standard_option(G_OPT_V_FIELD);
    opt_layer->answer = "1";

    opt_output = G_define_standard_option(G_OPT_V_OUTPUT);
    opt_output->description = _("Output vector map (copy of input with "
                                  "scaling-result attribute columns; or "
                                  "newly synthesised crater polygons "
                                  "when impactors= is used)");

    /* ---- Planetary body ---- */
    opt_body = G_define_option();
    opt_body->key         = "body";
    opt_body->type        = TYPE_STRING;
    opt_body->required    = NO;
    opt_body->answer      = "moon";
    opt_body->options     = p_crater_body_options();
    opt_body->description = _("Planetary body (sets default g, "
                                "target_density, target_type)");

    /* ---- Impactor parameters ---- */
    opt_vel = G_define_option();
    opt_vel->key          = "impactor_velocity";
    opt_vel->type         = TYPE_DOUBLE;
    opt_vel->required     = NO;
    opt_vel->answer       = "20000";
    opt_vel->description  = _("Impactor velocity [m/s] "
                                "(default 20000 = 20 km/s typical asteroid)");

    opt_angle = G_define_option();
    opt_angle->key         = "impactor_angle";
    opt_angle->type        = TYPE_DOUBLE;
    opt_angle->required    = NO;
    opt_angle->answer      = "45";
    opt_angle->description = _("Impactor angle from horizontal [degrees] "
                                 "(default 45, most probable)");

    opt_proj_density = G_define_option();
    opt_proj_density->key          = "impactor_density";
    opt_proj_density->type         = TYPE_DOUBLE;
    opt_proj_density->required     = NO;
    opt_proj_density->answer       = "3000";
    opt_proj_density->description  = _("Impactor (projectile) density "
                                         "[kg/m^3] (default 3000 = stony)");

    opt_proj_diam = G_define_option();
    opt_proj_diam->key          = "impactor_diameter";
    opt_proj_diam->type         = TYPE_DOUBLE;
    opt_proj_diam->required     = NO;
    opt_proj_diam->description  = _("Impactor diameter [m] "
                                      "(forward mode only - if omitted, "
                                      "estimated from crater rim diameter)");

    /* ---- Optional target overrides (scalar) ---- */
    opt_g_override = G_define_option();
    opt_g_override->key         = "gravity";
    opt_g_override->type        = TYPE_DOUBLE;
    opt_g_override->required    = NO;
    opt_g_override->description = _("Override surface gravity [m/s^2] "
                                      "(default from body database)");

    opt_targ_density_override = G_define_option();
    opt_targ_density_override->key         = "target_density";
    opt_targ_density_override->type        = TYPE_DOUBLE;
    opt_targ_density_override->required    = NO;
    opt_targ_density_override->description = _("Override target surface "
                                                 "density [kg/m^3]");

    opt_targ_type_override = G_define_option();
    opt_targ_type_override->key      = "target_type";
    opt_targ_type_override->type     = TYPE_INTEGER;
    opt_targ_type_override->required = NO;
    opt_targ_type_override->options  = "1,2,3";
    opt_targ_type_override->description =
        _("Override target type (1=water/ice, 2=loose sand/regolith, "
          "3=competent rock/saturated soil)");

    opt_dd_simple = G_define_option();
    opt_dd_simple->key         = "dd_simple";
    opt_dd_simple->type        = TYPE_DOUBLE;
    opt_dd_simple->required    = NO;
    opt_dd_simple->description = _("Override SIMPLE-crater depth/diameter "
                                     "ratio (0..1). Default: per-body value "
                                     "from the database (Moon 0.196, "
                                     "Mars 0.150, Mercury 0.180, Venus 0.140, "
                                     "Earth 0.130, Vesta 0.180, Ceres 0.170, "
                                     "icy moons 0.150, rubble piles 0.200, "
                                     "custom 0.196). Complex craters scale "
                                     "down from this value automatically.");

    /* ---- Optional per-crater geology rasters ---- */
    opt_geol_density = G_define_standard_option(G_OPT_R_INPUT);
    opt_geol_density->key         = "surface_density_map";
    opt_geol_density->required    = NO;
    opt_geol_density->description = _("Optional surface-layer density raster "
                                        "[kg/m^3], sampled at each crater "
                                        "centroid (overrides scalar "
                                        "target_density for that crater)");

    opt_surf_thick = G_define_option();
    opt_surf_thick->key         = "surface_thickness";
    opt_surf_thick->type        = TYPE_DOUBLE;
    opt_surf_thick->required    = NO;
    opt_surf_thick->description = _("Representative thickness [m] of the "
                                      "surface density layer (used together "
                                      "with subsurface_density_map for "
                                      "depth-weighted effective density)");

    opt_sub_density = G_define_standard_option(G_OPT_R_INPUT);
    opt_sub_density->key         = "subsurface_density_map";
    opt_sub_density->required    = NO;
    opt_sub_density->description = _("Optional subsurface-layer density "
                                       "raster [kg/m^3], sampled at each "
                                       "crater centroid; used together with "
                                       "subsurface_thickness for "
                                       "depth-weighted effective density");

    opt_sub_thick = G_define_option();
    opt_sub_thick->key         = "subsurface_thickness";
    opt_sub_thick->type        = TYPE_DOUBLE;
    opt_sub_thick->required    = NO;
    opt_sub_thick->description = _("Representative thickness [m] of the "
                                     "subsurface density layer (default: "
                                     "infinite, i.e. extends below the "
                                     "deepest excavation)");

    opt_geol_type = G_define_standard_option(G_OPT_R_INPUT);
    opt_geol_type->key         = "geology_type";
    opt_geol_type->required    = NO;
    opt_geol_type->description = _("Optional surface material-type raster "
                                     "(1/2/3), sampled at each crater "
                                     "centroid");

    opt_dd_map = G_define_standard_option(G_OPT_R_INPUT);
    opt_dd_map->key         = "dd_simple_map";
    opt_dd_map->required    = NO;
    opt_dd_map->description = _("Optional simple-crater depth/diameter "
                                  "raster (0..1), sampled at each crater "
                                  "centroid. Overrides dd_simple= scalar "
                                  "and the body database default for that "
                                  "crater. Enables spatial variation of "
                                  "d/D across the mapping area.");

    /* ---- Optional DEM ---- */
    opt_dem = G_define_standard_option(G_OPT_R_INPUT);
    opt_dem->key         = "dem";
    opt_dem->required    = NO;
    opt_dem->description = _("Optional DEM raster [m], used to measure "
                              "crater depth directly (rim - centre)");

    /* ---- Impactor-points mode: attribute column names ---- */
    opt_col_v = G_define_option();
    opt_col_v->key         = "col_velocity";
    opt_col_v->type        = TYPE_STRING;
    opt_col_v->required    = NO;
    opt_col_v->answer      = "velocity";
    opt_col_v->description = _("Attribute column with impactor velocity "
                                 "[m/s] (impactor-points mode)");

    opt_col_a = G_define_option();
    opt_col_a->key         = "col_angle";
    opt_col_a->type        = TYPE_STRING;
    opt_col_a->required    = NO;
    opt_col_a->answer      = "impact_angle";
    opt_col_a->description = _("Attribute column with impact angle from "
                                 "local DEM surface [deg, 90 = normal]");

    opt_col_az = G_define_option();
    opt_col_az->key         = "col_azimuth";
    opt_col_az->type        = TYPE_STRING;
    opt_col_az->required    = NO;
    opt_col_az->answer      = "azimuth";
    opt_col_az->description = _("Attribute column with downrange azimuth "
                                  "[deg, 0 = north, clockwise]");

    opt_col_rho = G_define_option();
    opt_col_rho->key         = "col_density";
    opt_col_rho->type        = TYPE_STRING;
    opt_col_rho->required    = NO;
    opt_col_rho->answer      = "density";
    opt_col_rho->description = _("Attribute column with impactor density "
                                   "(unit set by density_unit=)");

    opt_col_L = G_define_option();
    opt_col_L->key         = "col_diameter";
    opt_col_L->type        = TYPE_STRING;
    opt_col_L->required    = NO;
    opt_col_L->answer      = "diameter";
    opt_col_L->description = _("Attribute column with impactor diameter "
                                 "[m, spheroid]");

    opt_dens_unit = G_define_option();
    opt_dens_unit->key         = "density_unit";
    opt_dens_unit->type        = TYPE_STRING;
    opt_dens_unit->required    = NO;
    opt_dens_unit->answer      = "kg_m3";
    opt_dens_unit->options     = "kg_m3,g_cm3";
    opt_dens_unit->description = _("Unit of the col_density column "
                                     "(g/cm^3 is multiplied by 1000)");

    /* ---- Flags ---- */
    flag_backward = G_define_flag();
    flag_backward->key         = 'b';
    flag_backward->description = _("Backward mode: estimate impactor "
                                     "diameter from observed crater rim "
                                     "(default: forward mode)");

    flag_overwrite_attrs = G_define_flag();
    flag_overwrite_attrs->key         = 'a';
    flag_overwrite_attrs->description = _("Overwrite existing attribute "
                                            "columns silently");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* ------------------------------------------------------------ */
    /* Resolve body defaults                                          */
    /* ------------------------------------------------------------ */
    const PCraterBody *body = p_crater_body_lookup(opt_body->answer);
    if (!body)
        G_fatal_error(_("Unknown planetary body '%s'"), opt_body->answer);

    int is_custom = (strcasecmp(body->name, "custom") == 0);
    if (is_custom) {
        /* User-defined body: gravity, target_density, target_type
         * must all be supplied explicitly. */
        if (!opt_g_override->answer || !opt_targ_density_override->answer
            || !opt_targ_type_override->answer) {
            G_fatal_error(_("body=custom requires gravity=, "
                              "target_density= and target_type= to all be "
                              "supplied explicitly"));
        }
    }

    double g_default      = body->g;
    double rho_t_default  = body->surface_density;
    int    ttype_default  = body->target_type;
    double dD_simple      = body->dD_simple > 0.0 ? body->dD_simple : 0.196;
    double Dsc_km         = body->dsc_km;  /* 0 = 1/g fallback */

    if (opt_g_override->answer)
        g_default = atof(opt_g_override->answer);
    if (opt_targ_density_override->answer)
        rho_t_default = atof(opt_targ_density_override->answer);
    if (opt_targ_type_override->answer)
        ttype_default = atoi(opt_targ_type_override->answer);
    if (opt_dd_simple->answer) {
        double v = atof(opt_dd_simple->answer);
        if (v <= 0.0 || v > 0.5)
            G_fatal_error(_("dd_simple=%.3f out of plausible range "
                              "(0, 0.5]; canonical lunar value is 0.196"),
                          v);
        dD_simple = v;
        G_message(_("Using user-specified simple d/D = %.3f (overriding "
                     "body default)"), dD_simple);
    }

    if (g_default <= 0.0 || rho_t_default <= 0.0 ||
        ttype_default < 1  || ttype_default > 3) {
        G_fatal_error(_("Invalid body parameters: gravity=%.4g m/s^2, "
                          "target_density=%.0f kg/m^3, target_type=%d "
                          "(must be 1/2/3)"),
                      g_default, rho_t_default, ttype_default);
    }

    double Vi           = atof(opt_vel->answer);
    double theta_deg    = atof(opt_angle->answer);
    double theta_rad    = theta_deg * M_PI / 180.0;
    double rho_p        = atof(opt_proj_density->answer);
    int    backward     = flag_backward->answer ? 1 : 0;
    double L_forward    = opt_proj_diam->answer
                            ? atof(opt_proj_diam->answer) : 0.0;

    G_message(_("Body: %s  (g=%.3f m/s^2, R=%.1f km, surf.rho=%.0f kg/m^3, "
                 "target_type=%d)"),
              body->name, g_default, body->radius_km,
              rho_t_default, ttype_default);
    G_message(_("Simple-to-complex transition for this body: %.2f km"),
              p_crater_simple_complex_D_body(g_default, Dsc_km) / 1000.0);

    /* ------------------------------------------------------------ */
    /* Mutually-exclusive input modes                                 */
    /* ------------------------------------------------------------ */
    int have_input     = opt_input->answer    && opt_input->answer[0];
    int have_impactors = opt_impactors->answer && opt_impactors->answer[0];
    if (have_input == have_impactors)
        G_fatal_error(_("Exactly one of input= (crater rim polygons) or "
                         "impactors= (impactor points) must be supplied "
                         "(got %s)"),
                      have_input ? "both" : "neither");

    /* ------------------------------------------------------------ */
    /* Impactor-points (synthesis) mode - delegate to helper module   */
    /* ------------------------------------------------------------ */
    if (have_impactors) {
        if (backward)
            G_fatal_error(_("impactors= requires forward mode "
                             "(-b cannot be combined with impactor synthesis)"));
        if (!opt_dem->answer)
            G_fatal_error(_("impactors= requires dem= (a DEM raster is "
                             "needed to sample elevation and slope/aspect "
                             "at each impact site)"));

        PCraterImpactorCols cols = {
            opt_col_v->answer,
            opt_col_a->answer,
            opt_col_az->answer,
            opt_col_rho->answer,
            opt_col_L->answer,
            strcmp(opt_dens_unit->answer, "g_cm3") == 0
        };
        /* Reuse the same dd_simple_map raster, if any, for synthesis. */
        int impactor_fd_dd = -1;
        if (opt_dd_map->answer)
            impactor_fd_dd = Rast_open_old(opt_dd_map->answer, "");
        PCraterBodyCtx ctx = {
            g_default, rho_t_default, ttype_default,
            dD_simple, impactor_fd_dd, Dsc_km
        };
        G_message(_("Synthesis mode: reading impactors from <%s>, "
                     "writing craters to <%s>"),
                  opt_impactors->answer, opt_output->answer);
        return p_crater_run_impactor_points(opt_impactors->answer,
                                              opt_layer->answer,
                                              opt_output->answer,
                                              opt_dem->answer,
                                              &cols, &ctx);
    }

    /* ============================================================= */
    /* Below: original polygon-rim scaling mode                       */
    /* ============================================================= */
    G_message(_("Impactor (scalar defaults): V=%.1f km/s, angle=%.1f deg, "
                 "rho_p=%.0f kg/m^3"),
              Vi / 1000.0, theta_deg, rho_p);

    struct Map_info In, Out;
    Vect_set_open_level(2);
    if (Vect_open_old2(&In, opt_input->answer, "",
                        opt_layer->answer) < 2)
        G_fatal_error(_("Unable to open vector map <%s> at level 2"),
                      opt_input->answer);

    int layer = Vect_get_field_number(&In, opt_layer->answer);

    if (Vect_open_new(&Out, opt_output->answer,
                       Vect_is_3d(&In)) < 0)
        G_fatal_error(_("Unable to create vector map <%s>"),
                      opt_output->answer);

    Vect_copy_head_data(&In, &Out);
    Vect_hist_copy(&In, &Out);
    Vect_hist_command(&Out);

    /* Copy all features straight through. */
    G_message(_("Copying input geometries to <%s>..."), opt_output->answer);
    Vect_copy_map_lines(&In, &Out);
    Vect_copy_tables(&In, &Out, layer);

    /* ------------------------------------------------------------ */
    /* Open optional geology / DEM rasters                            */
    /* ------------------------------------------------------------ */
    int fd_geol_d = -1, fd_sub_d = -1, fd_geol_t = -1;
    int fd_dem = -1,    fd_dd_map = -1;
    if (opt_geol_density->answer)
        fd_geol_d = Rast_open_old(opt_geol_density->answer, "");
    if (opt_sub_density->answer)
        fd_sub_d  = Rast_open_old(opt_sub_density->answer, "");
    if (opt_geol_type->answer)
        fd_geol_t = Rast_open_old(opt_geol_type->answer, "");
    if (opt_dem->answer)
        fd_dem    = Rast_open_old(opt_dem->answer, "");
    if (opt_dd_map->answer)
        fd_dd_map = Rast_open_old(opt_dd_map->answer, "");

    double h_surf = opt_surf_thick->answer
                      ? atof(opt_surf_thick->answer) : 0.0;
    double h_sub  = opt_sub_thick->answer
                      ? atof(opt_sub_thick->answer)  : 0.0;
    if (h_surf > 0.0)
        G_message(_("Surface layer thickness: %.1f m"), h_surf);
    if (fd_sub_d >= 0)
        G_message(_("Subsurface density layer active "
                     "(thickness %s)"),
                  h_sub > 0.0 ? "from option" : "treated as infinite");

    /* ------------------------------------------------------------ */
    /* Get attribute table info, add result columns                   */
    /* ------------------------------------------------------------ */
    struct field_info *Fi = Vect_get_field(&Out, layer);
    if (!Fi)
        G_fatal_error(_("Vector <%s> has no attribute table on layer %d"),
                      opt_output->answer, layer);

    dbDriver *driver = db_start_driver_open_database(Fi->driver, Fi->database);
    if (!driver)
        G_fatal_error(_("Unable to open database <%s> by driver <%s>"),
                      Fi->database, Fi->driver);
    db_set_error_handler_driver(driver);

    const char *cols[] = {
        "D_eq", "Dat_pi", "Dat_gault", "Dat_yield",
        "proj_pi", "proj_gault", "proj_yield",
        "kinetic_J", "tnt_kt",
        "Df_pi", "depth_pred", "depth_dem", "dD_ratio",
        "rho_eff",
        NULL
    };
    (void)flag_overwrite_attrs; /* ALTER ... ADD COLUMN errors are tolerated */
    for (int i = 0; cols[i]; i++)
        add_column(driver, Fi->table, cols[i]);

    /* ------------------------------------------------------------ */
    /* Iterate over polygons (areas)                                  */
    /* ------------------------------------------------------------ */
    int n_areas = Vect_get_num_areas(&In);
    if (n_areas <= 0)
        G_warning(_("Input vector contains no areas; only feature "
                     "geometries were copied"));

    struct line_pnts *Pts = Vect_new_line_struct();
    struct line_cats *Cats = Vect_new_cats_struct();
    int n_processed = 0, n_skipped = 0;

    for (int a = 1; a <= n_areas; a++) {
        G_percent(a, n_areas, 5);

        int centroid = Vect_get_area_centroid(&In, a);
        if (centroid <= 0) {
            n_skipped++;
            continue;
        }
        Vect_read_line(&In, Pts, Cats, centroid);
        if (Pts->n_points < 1) {
            n_skipped++;
            continue;
        }
        double cx = Pts->x[0];
        double cy = Pts->y[0];
        int cat = -1;
        if (Vect_cat_get(Cats, layer, &cat) == 0) {
            /* no category on this layer -> skip attribute writeback */
            n_skipped++;
            continue;
        }

        /* Polygon boundary - measured diameter. */
        Vect_get_area_points(&In, a, Pts);
        double area_m2 = polygon_area_m2(Pts);
        double D_eq    = 2.0 * sqrt(area_m2 / M_PI);

        /* Per-crater geology overrides. */
        double rho_t = rho_t_default;
        double rho_surf_cell = rho_t_default;
        double rho_sub_cell  = 0.0;
        int    ttype = ttype_default;
        double dD_cell = dD_simple;   /* falls back to scalar / body default */
        if (fd_geol_d >= 0) {
            double v;
            if (sample_raster(fd_geol_d, cx, cy, &v) && v > 0.0)
                rho_surf_cell = v;
        }
        if (fd_sub_d >= 0) {
            double v;
            if (sample_raster(fd_sub_d, cx, cy, &v) && v > 0.0)
                rho_sub_cell = v;
        }
        if (fd_geol_t >= 0) {
            double v;
            if (sample_raster(fd_geol_t, cx, cy, &v)) {
                int t = (int)(v + 0.5);
                if (t >= 1 && t <= 3) ttype = t;
            }
        }
        if (fd_dd_map >= 0) {
            double v;
            if (sample_raster(fd_dd_map, cx, cy, &v) && v > 0.0 && v <= 0.5)
                dD_cell = v;
        }
        else if (!opt_dd_simple->answer) {
            /* Lowest-priority fallback: a `dD_simple` column on the
             * input vector's attribute table - this is the column
             * baked by p.crater.draw at detection time. Honoured only
             * if the user supplied neither dd_simple_map= nor
             * dd_simple= as an explicit override.                    */
            char sql[256];
            snprintf(sql, sizeof(sql),
                     "SELECT dD_simple FROM %s WHERE %s = %d",
                     Fi->table, Fi->key, cat);
            dbString stmt;
            db_init_string(&stmt);
            db_set_string(&stmt, sql);
            dbCursor cur;
            if (db_open_select_cursor(driver, &stmt, &cur,
                                         DB_SEQUENTIAL) == DB_OK) {
                int more;
                if (db_fetch(&cur, DB_NEXT, &more) == DB_OK && more) {
                    dbTable *T  = db_get_cursor_table(&cur);
                    dbColumn *C = db_get_table_column(T, 0);
                    dbValue  *V = db_get_column_value(C);
                    if (!db_test_value_isnull(V)) {
                        double v = db_get_value_double(V);
                        if (v > 0.0 && v <= 0.5) dD_cell = v;
                    }
                }
                db_close_cursor(&cur);
            }
            db_free_string(&stmt);
        }
        /* Initial estimate uses surface layer only - will be refined
         * once we have a transient-diameter estimate below.           */
        rho_t = rho_surf_cell;

        /* DEM depth: rim mean elevation - centre elevation. */
        double depth_dem = NAN;
        if (fd_dem >= 0) {
            double z_center, z_rim_sum = 0.0;
            int z_rim_n = 0;
            int have_centre = sample_raster(fd_dem, cx, cy, &z_center);
            /* Sample rim points (skip closing duplicate). */
            for (int i = 0; i < Pts->n_points - 1; i++) {
                double zr;
                if (sample_raster(fd_dem, Pts->x[i], Pts->y[i], &zr)) {
                    z_rim_sum += zr;
                    z_rim_n++;
                }
            }
            if (have_centre && z_rim_n > 0)
                depth_dem = (z_rim_sum / z_rim_n) - z_center;
        }

        /* ---- Compute scaling ---- */
        double Dat_pi = NAN, Dat_g = NAN, Dat_y = NAN;
        double proj_pi = NAN, proj_g = NAN, proj_y = NAN;
        double W = NAN, tnt_kt = NAN;
        double Df_pi = NAN, depth_pred = NAN, dD_ratio = NAN;

        if (backward || L_forward <= 0.0) {
            /* Backward mode: use measured D_eq as apparent transient Dat.
             * Excavation depth ~ Dat/3 is known up-front, so the effective
             * density can be computed once and used for all scaling laws. */
            double Dat = D_eq;
            rho_t = effective_density(Dat, rho_surf_cell, h_surf,
                                       rho_sub_cell, h_sub);
            proj_pi = p_crater_pi_L   (Vi, rho_p, rho_t, Dat, g_default);
            proj_g  = p_crater_gault_L(Dat, Vi, rho_p, rho_t, theta_rad, ttype);
            proj_y  = p_crater_yield_L(Vi, rho_p, rho_t, Dat);

            /* Use Pi-estimated projectile to back-compute energy. */
            if (proj_pi > 0.0) {
                W      = p_crater_kinetic_energy(rho_p, proj_pi, Vi);
                tnt_kt = p_crater_tnt_kt(W);
            }
            Dat_pi    = Dat;
            Df_pi     = p_crater_final_diameter(Dat, g_default, Dsc_km);
            depth_pred = p_crater_depth(Df_pi, g_default, dD_cell, Dsc_km);
            if (Df_pi > 0.0) dD_ratio = depth_pred / Df_pi;
        } else {
            /* Forward mode: known L_forward. Compute Dat with surface
             * density first, then refine with depth-weighted effective
             * density (one fixed-point iteration is plenty - density
             * appears only at exponent ~ -1/3 in Pi-scaling).            */
            W      = p_crater_kinetic_energy(rho_p, L_forward, Vi);
            tnt_kt = p_crater_tnt_kt(W);
            double Dat_est = p_crater_pi_Dat(W, rho_p, rho_surf_cell,
                                                L_forward, g_default);
            rho_t  = effective_density(Dat_est, rho_surf_cell, h_surf,
                                         rho_sub_cell, h_sub);
            Dat_pi = p_crater_pi_Dat   (W, rho_p, rho_t, L_forward, g_default);
            Dat_g  = p_crater_gault_Dat(W, rho_p, rho_t, theta_rad, ttype);
            Dat_y  = p_crater_yield_Dat(W, rho_p, rho_t, L_forward);
            Df_pi  = p_crater_final_diameter(Dat_pi, g_default, Dsc_km);
            depth_pred = p_crater_depth(Df_pi, g_default, dD_cell, Dsc_km);
            if (Df_pi > 0.0) dD_ratio = depth_pred / Df_pi;

            /* Store the forward projectile for completeness. */
            proj_pi = L_forward;
            proj_g  = L_forward;
            proj_y  = L_forward;
        }

        /* ---- Write attributes ---- */
        update_value(driver, Fi->table, Fi->key, cat, "D_eq",      D_eq);
        update_value(driver, Fi->table, Fi->key, cat, "Dat_pi",    Dat_pi);
        update_value(driver, Fi->table, Fi->key, cat, "Dat_gault", Dat_g);
        update_value(driver, Fi->table, Fi->key, cat, "Dat_yield", Dat_y);
        update_value(driver, Fi->table, Fi->key, cat, "proj_pi",   proj_pi);
        update_value(driver, Fi->table, Fi->key, cat, "proj_gault",proj_g);
        update_value(driver, Fi->table, Fi->key, cat, "proj_yield",proj_y);
        update_value(driver, Fi->table, Fi->key, cat, "kinetic_J", W);
        update_value(driver, Fi->table, Fi->key, cat, "tnt_kt",    tnt_kt);
        update_value(driver, Fi->table, Fi->key, cat, "Df_pi",     Df_pi);
        update_value(driver, Fi->table, Fi->key, cat, "depth_pred",depth_pred);
        update_value(driver, Fi->table, Fi->key, cat, "depth_dem", depth_dem);
        update_value(driver, Fi->table, Fi->key, cat, "dD_ratio",  dD_ratio);
        update_value(driver, Fi->table, Fi->key, cat, "rho_eff",   rho_t);

        n_processed++;
    }

    /* ------------------------------------------------------------ */
    /* Cleanup                                                        */
    /* ------------------------------------------------------------ */
    Vect_destroy_line_struct(Pts);
    Vect_destroy_cats_struct(Cats);

    db_close_database_shutdown_driver(driver);

    if (fd_geol_d >= 0) Rast_close(fd_geol_d);
    if (fd_sub_d  >= 0) Rast_close(fd_sub_d);
    if (fd_geol_t >= 0) Rast_close(fd_geol_t);
    if (fd_dem    >= 0) Rast_close(fd_dem);
    if (fd_dd_map >= 0) Rast_close(fd_dd_map);

    Vect_build(&Out);
    Vect_close(&In);
    Vect_close(&Out);

    G_message(_("Done. %d craters processed, %d skipped (no centroid/cat)."),
              n_processed, n_skipped);
    return EXIT_SUCCESS;
}
