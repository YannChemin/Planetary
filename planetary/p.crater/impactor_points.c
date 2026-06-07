/****************************************************************************
 *
 * MODULE:       p.crater (impactor_points.c)
 * PURPOSE:      Forward-mode synthesis of crater geometries from a vector
 *               of impactor points with per-feature attributes and a DEM.
 *
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
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

#include "impactor_points.h"
#include "crater_eq.h"
#include "planet_db.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Tuning constants for the ricochet chain.
 * These reproduce the qualitative behaviour reported by Schultz & Gault
 * (1990) for very-low-angle impacts on planetary surfaces: a primary
 * elongated crater followed by 1-4 decreasing-size secondary craters
 * along the downrange azimuth, with energy and projectile size
 * decreasing by roughly half per hop. Real ricochet dynamics depend on
 * target strength and impactor cohesion; the constants here are
 * defensible defaults but can be tuned via future options.            */
#define BOUNCE_THETA_DEG    5.0   /* below this angle, bouncing starts  */
#define BOUNCE_HOP_FACTOR   4.0   /* hop distance = this * D_previous   */
#define BOUNCE_V_FACTOR     0.50  /* velocity retained after each bounce */
#define BOUNCE_L_FACTOR     0.60  /* projectile size after fragmentation */
#define BOUNCE_MAX          4     /* number of bounces after primary     */
#define BOUNCE_MIN_L_M      1.0   /* stop when projectile < 1 m           */
#define BOUNCE_MIN_V_MS     100.0 /* or velocity < 100 m/s               */

/* Threshold below which a crater is reported as 'ellipse' rather than
 * 'circle' (in degrees from local surface).                            */
#define ELLIPSE_THETA_DEG   30.0

/* Number of vertices in each output polygon ring. */
#define POLY_VERTS          48

/* ------------------------------------------------------------------ */
/* DEM sampling and 3x3 slope/aspect.                                   */
/* ------------------------------------------------------------------ */

static int sample_raster_at(int fd, double x, double y,
                             const struct Cell_head *win, double *val)
{
    int row = (int)Rast_northing_to_row(y, win);
    int col = (int)Rast_easting_to_col (x, win);
    if (row < 0 || row >= win->rows || col < 0 || col >= win->cols)
        return 0;
    DCELL *buf = Rast_allocate_d_buf();
    Rast_get_d_row(fd, buf, row);
    int ok = !Rast_is_d_null_value(&buf[col]);
    if (ok) *val = buf[col];
    G_free(buf);
    return ok;
}

/*
 * Horn (1981) 3x3 slope/aspect at world coordinates (x, y).
 * Returns 1 on success.
 */
static int sample_slope_aspect(int fd_dem, double x, double y,
                                const struct Cell_head *win,
                                double *elev,
                                double *slope_rad,
                                double *aspect_rad)
{
    double ex = win->ew_res;
    double ny = win->ns_res;
    double z[3][3];
    int    ok = 1;
    /* (0,0)=NW, (1,1)=centre, (2,2)=SE; world (x+i*dx, y-j*dy)         */
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double xi = x + (i - 1) * ex;
            double yj = y - (j - 1) * ny;
            if (!sample_raster_at(fd_dem, xi, yj, win, &z[j][i]))
                ok = 0;
        }
    }
    if (!ok) return 0;

    /* Horn (1981) finite differences. */
    double dzdx = ((z[0][2] + 2.0 * z[1][2] + z[2][2]) -
                   (z[0][0] + 2.0 * z[1][0] + z[2][0])) / (8.0 * ex);
    double dzdy = ((z[2][0] + 2.0 * z[2][1] + z[2][2]) -
                   (z[0][0] + 2.0 * z[0][1] + z[0][2])) / (8.0 * ny);

    *elev       = z[1][1];
    *slope_rad  = atan(sqrt(dzdx * dzdx + dzdy * dzdy));
    /* Aspect: 0 = east, anticlockwise to north. Convert to compass (0
     * = north, clockwise) later if needed. We keep math convention.   */
    *aspect_rad = atan2(dzdy, -dzdx);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Attribute-table reader.                                              */
/* ------------------------------------------------------------------ */

static int select_double(dbDriver *driver, const char *table,
                          const char *key_col, int cat,
                          const char *col, double *out)
{
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT %s FROM %s WHERE %s = %d", col, table, key_col, cat);
    dbString stmt;
    db_init_string(&stmt);
    db_set_string(&stmt, sql);
    dbCursor cursor;
    if (db_open_select_cursor(driver, &stmt, &cursor, DB_SEQUENTIAL) != DB_OK) {
        db_free_string(&stmt);
        return 0;
    }
    int more, got = 0;
    if (db_fetch(&cursor, DB_NEXT, &more) == DB_OK && more) {
        dbTable  *t = db_get_cursor_table(&cursor);
        dbColumn *c = db_get_table_column(t, 0);
        dbValue  *v = db_get_column_value(c);
        if (!db_test_value_isnull(v)) {
            int ct = db_sqltype_to_Ctype(db_get_column_sqltype(c));
            if (ct == DB_C_TYPE_INT)
                *out = (double)db_get_value_int(v);
            else if (ct == DB_C_TYPE_DOUBLE)
                *out = db_get_value_double(v);
            else
                got = 0;
            got = 1;
        }
    }
    db_close_cursor(&cursor);
    db_free_string(&stmt);
    return got;
}

/* ------------------------------------------------------------------ */
/* Output table creation + row insertion.                               */
/* ------------------------------------------------------------------ */

static const char *kOutColumns =
    "cat INTEGER PRIMARY KEY,"
    "parent_id INTEGER,"
    "bounce INTEGER,"
    "kind VARCHAR(16),"
    "D_major DOUBLE PRECISION,"
    "D_minor DOUBLE PRECISION,"
    "azimuth_deg DOUBLE PRECISION,"
    "theta_local_deg DOUBLE PRECISION,"
    "Df_pi DOUBLE PRECISION,"
    "depth_pred DOUBLE PRECISION,"
    "proj_L_m DOUBLE PRECISION,"
    "V_m_s DOUBLE PRECISION,"
    "kinetic_J DOUBLE PRECISION,"
    "tnt_kt DOUBLE PRECISION,"
    "elev_m DOUBLE PRECISION,"
    "slope_deg DOUBLE PRECISION,"
    "aspect_deg DOUBLE PRECISION";

static void exec_sql(dbDriver *driver, const char *sql)
{
    dbString stmt;
    db_init_string(&stmt);
    db_set_string(&stmt, sql);
    if (db_execute_immediate(driver, &stmt) != DB_OK)
        G_warning(_("SQL failed: %s"), sql);
    db_free_string(&stmt);
}

/* ------------------------------------------------------------------ */
/* Write one closed-ring boundary + a centroid with attributes.         */
/* ------------------------------------------------------------------ */

static void write_polygon_with_attrs(struct Map_info *Out,
                                       dbDriver *driver,
                                       const char *table,
                                       int *cat_counter,
                                       int parent_id, int bounce,
                                       const char *kind,
                                       double cx, double cy,
                                       double D_major, double D_minor,
                                       double azimuth_rad,
                                       double theta_deg,
                                       double Df, double depth,
                                       double L, double V,
                                       double W_J,
                                       double elev, double slope_deg,
                                       double aspect_deg)
{
    /* Build the ellipse/circle ring around (cx, cy). */
    double a = 0.5 * D_major;
    double b = 0.5 * D_minor;
    double cs = cos(azimuth_rad), sn = sin(azimuth_rad);

    struct line_pnts *Pts = Vect_new_line_struct();
    struct line_cats *Cats = Vect_new_cats_struct();

    for (int i = 0; i < POLY_VERTS; i++) {
        double t  = 2.0 * M_PI * i / POLY_VERTS;
        /* Local frame: major axis along +x. */
        double lx = a * cos(t);
        double ly = b * sin(t);
        /* Rotate so major axis points to azimuth (measured anticlockwise
         * from east in math convention, matching atan2 outputs).        */
        double wx = cx + cs * lx - sn * ly;
        double wy = cy + sn * lx + cs * ly;
        Vect_append_point(Pts, wx, wy, 0.0);
    }
    /* Close the ring with a BIT-EXACT copy of the first vertex. The
     * obvious i=POLY_VERTS step generates a vertex that differs from
     * vertex 0 by ~3e-13 due to sin(2*pi) residual, which the GRASS
     * topology builder rejects as an open ring (giving 0 areas).      */
    Vect_append_point(Pts, Pts->x[0], Pts->y[0], 0.0);
    Vect_write_line(Out, GV_BOUNDARY, Pts, Cats);

    /* Centroid carrying the category. */
    Vect_reset_line(Pts);
    Vect_reset_cats(Cats);
    Vect_append_point(Pts, cx, cy, 0.0);
    int cat = (*cat_counter)++;
    Vect_cat_set(Cats, 1, cat);
    Vect_write_line(Out, GV_CENTROID, Pts, Cats);

    /* INSERT attribute row. */
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO %s ("
             "cat,parent_id,bounce,kind,D_major,D_minor,azimuth_deg,"
             "theta_local_deg,Df_pi,depth_pred,proj_L_m,V_m_s,kinetic_J,"
             "tnt_kt,elev_m,slope_deg,aspect_deg) VALUES ("
             "%d,%d,%d,'%s',%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,"
             "%.6g,%.6g,%.6g,%.6g,%.6g)",
             table, cat, parent_id, bounce, kind,
             D_major, D_minor, azimuth_rad * 180.0 / M_PI,
             theta_deg, Df, depth, L, V, W_J, W_J / 4.184e12,
             elev, slope_deg, aspect_deg);
    exec_sql(driver, sql);

    Vect_destroy_line_struct(Pts);
    Vect_destroy_cats_struct(Cats);
}

/* ------------------------------------------------------------------ */
/* Generate the primary + (optional) bounce craters for one impactor.   */
/* ------------------------------------------------------------------ */

static void generate_chain(struct Map_info *Out,
                            dbDriver *driver, const char *table,
                            int *cat_counter,
                            int parent_id,
                            double x, double y,
                            double elev, double slope_deg, double aspect_deg,
                            double V0, double theta_rad, double az_rad,
                            double rho_p, double L0,
                            const PCraterBodyCtx *body,
                            int *n_polys_out)
{
    /* Use the current scaling library for all per-bounce computations.
     * For oblique impacts we evaluate the Pi-scaling Dat at the *given*
     * angle (Gault uses sin(theta)^(1/3) directly; Pi has no explicit
     * angle factor, so we account for obliquity entirely via the
     * a/b ellipse axis ratio).                                         */
    double V = V0, L = L0;
    double theta_deg = theta_rad * 180.0 / M_PI;
    int    n_bounce  = 0;
    double bx = x, by = y;
    double D_prev = 0.0;

    int max_steps = 1 + BOUNCE_MAX;  /* primary + up to BOUNCE_MAX bounces */
    for (int step = 0; step < max_steps; step++) {
        if (V < BOUNCE_MIN_V_MS || L < BOUNCE_MIN_L_M) break;

        double W   = p_crater_kinetic_energy(rho_p, L, V);
        double Dat = p_crater_pi_Dat(W, rho_p, body->rho_t_default, L, body->g);
        double Df  = p_crater_final_diameter(Dat, body->g, body->dsc_km);
        /* Per-impactor d/D from raster (if supplied), else body default. */
        double dD_here = body->dD_simple;
        if (body->fd_dd_map >= 0) {
            struct Cell_head win;
            Rast_get_window(&win);
            int row_i = (int)Rast_northing_to_row(by, &win);
            int col_i = (int)Rast_easting_to_col (bx, &win);
            if (row_i >= 0 && row_i < win.rows &&
                col_i >= 0 && col_i < win.cols) {
                DCELL *buf = Rast_allocate_d_buf();
                Rast_get_d_row(body->fd_dd_map, buf, row_i);
                if (!Rast_is_d_null_value(&buf[col_i]) &&
                     buf[col_i] > 0.0 && buf[col_i] <= 0.5)
                    dD_here = buf[col_i];
                G_free(buf);
            }
        }
        double dp  = p_crater_depth(Df, body->g, dD_here, body->dsc_km);

        /* Major axis follows Df; minor axis follows the aspect-ratio
         * law b/a = sin(theta)^(1/3) (Pierazzo & Melosh 2000), clipped
         * below 0.2 to avoid a degenerate line for theta near 0.       */
        double D_major = Df;
        double s = sin(theta_rad);
        if (s < 0.005) s = 0.005;
        double ratio = pow(s, 1.0 / 3.0);
        if (ratio > 1.0) ratio = 1.0;
        if (ratio < 0.20) ratio = 0.20;
        double D_minor = D_major * ratio;

        const char *kind = (theta_deg >= ELLIPSE_THETA_DEG)
                             ? "circle" : "ellipse";

        write_polygon_with_attrs(Out, driver, table, cat_counter,
                                  parent_id, n_bounce, kind,
                                  bx, by, D_major, D_minor, az_rad,
                                  theta_deg, Df, dp, L, V, W,
                                  elev, slope_deg, aspect_deg);
        (*n_polys_out)++;
        D_prev = D_major;

        /* Bouncing only at very grazing angles. */
        if (theta_deg >= BOUNCE_THETA_DEG) break;

        /* Advance downrange by BOUNCE_HOP_FACTOR * D_prev. */
        bx += BOUNCE_HOP_FACTOR * D_prev * cos(az_rad);
        by += BOUNCE_HOP_FACTOR * D_prev * sin(az_rad);

        /* Attenuate for next iteration. */
        V *= BOUNCE_V_FACTOR;
        L *= BOUNCE_L_FACTOR;
        n_bounce++;
    }
}

/* ================================================================== */
/* Public entry point                                                  */
/* ================================================================== */

int p_crater_run_impactor_points(const char *impactors_name,
                                  const char *layer_name,
                                  const char *out_name,
                                  const char *dem_name,
                                  const PCraterImpactorCols *cols,
                                  const PCraterBodyCtx *body)
{
    if (!dem_name || !dem_name[0])
        G_fatal_error(_("impactor-point synthesis requires dem= raster"));

    /* ---- Open input point vector ---- */
    struct Map_info In;
    Vect_set_open_level(2);
    if (Vect_open_old2(&In, impactors_name, "", layer_name) < 2)
        G_fatal_error(_("Cannot open vector <%s> at level 2"),
                      impactors_name);
    int layer = Vect_get_field_number(&In, layer_name);

    struct field_info *Fi_in = Vect_get_field(&In, layer);
    if (!Fi_in)
        G_fatal_error(_("Vector <%s> has no attribute table on layer %d"),
                      impactors_name, layer);
    dbDriver *drv_in = db_start_driver_open_database(Fi_in->driver,
                                                       Fi_in->database);
    if (!drv_in)
        G_fatal_error(_("Cannot open input attribute database"));
    db_set_error_handler_driver(drv_in);

    /* ---- DEM ---- */
    int fd_dem = Rast_open_old(dem_name, "");
    struct Cell_head dem_win;
    Rast_get_cellhd(dem_name, "", &dem_win);

    /* ---- Output vector ---- */
    struct Map_info Out;
    if (Vect_open_new(&Out, out_name, 0) < 0)
        G_fatal_error(_("Cannot create output vector <%s>"), out_name);
    Vect_hist_command(&Out);

    /* ---- Create output attribute table ---- */
    struct field_info *Fi_out = Vect_default_field_info(&Out, 1, NULL,
                                                          GV_1TABLE);
    Vect_map_add_dblink(&Out, 1, NULL,
                         Fi_out->table, GV_KEY_COLUMN,
                         Fi_out->database, Fi_out->driver);
    dbDriver *drv_out = db_start_driver_open_database(Fi_out->driver,
                                                        Fi_out->database);
    if (!drv_out)
        G_fatal_error(_("Cannot open output attribute database"));
    db_set_error_handler_driver(drv_out);

    {
        char create_sql[2048];
        snprintf(create_sql, sizeof(create_sql),
                 "CREATE TABLE %s (%s)", Fi_out->table, kOutColumns);
        exec_sql(drv_out, create_sql);
        char idx_sql[256];
        snprintf(idx_sql, sizeof(idx_sql),
                 "CREATE INDEX %s_idx ON %s (cat)",
                 Fi_out->table, Fi_out->table);
        exec_sql(drv_out, idx_sql);
    }

    /* ---- Iterate input points ---- */
    struct line_pnts *Pts  = Vect_new_line_struct();
    struct line_cats *Cats = Vect_new_cats_struct();

    int cat_counter = 1;
    int n_points = 0, n_polys = 0, n_skipped = 0;
    int type;
    Vect_rewind(&In);
    while ((type = Vect_read_next_line(&In, Pts, Cats)) > 0) {
        if (type != GV_POINT) continue;
        n_points++;
        int parent_cat = -1;
        if (Vect_cat_get(Cats, layer, &parent_cat) == 0) {
            n_skipped++;
            continue;
        }
        double x = Pts->x[0];
        double y = Pts->y[0];

        /* Required attributes (kg/m^3 default, g/cm3 if flagged). */
        double V, theta_deg, az_deg, rho_p_in, L_m;
        int ok = 1;
        ok &= select_double(drv_in, Fi_in->table, Fi_in->key, parent_cat,
                            cols->col_velocity, &V);
        ok &= select_double(drv_in, Fi_in->table, Fi_in->key, parent_cat,
                            cols->col_angle, &theta_deg);
        ok &= select_double(drv_in, Fi_in->table, Fi_in->key, parent_cat,
                            cols->col_azimuth, &az_deg);
        ok &= select_double(drv_in, Fi_in->table, Fi_in->key, parent_cat,
                            cols->col_density, &rho_p_in);
        ok &= select_double(drv_in, Fi_in->table, Fi_in->key, parent_cat,
                            cols->col_diameter, &L_m);
        if (!ok) {
            G_warning(_("Impactor cat %d: missing/NULL attribute in one of "
                         "%s/%s/%s/%s/%s - skipped"),
                      parent_cat,
                      cols->col_velocity, cols->col_angle,
                      cols->col_azimuth, cols->col_density,
                      cols->col_diameter);
            n_skipped++;
            continue;
        }

        double rho_p = cols->density_g_cm3 ? rho_p_in * 1000.0 : rho_p_in;
        if (rho_p <= 0.0 || V <= 0.0 || L_m <= 0.0) {
            n_skipped++;
            continue;
        }
        double theta_rad = theta_deg * M_PI / 180.0;
        /* Convert "compass" azimuth (0 = north, clockwise) to math
         * convention (0 = east, anticlockwise) used by the ellipse
         * generator.                                                    */
        double az_math   = (90.0 - az_deg) * M_PI / 180.0;

        /* Sample DEM. */
        double elev = 0.0, slope_rad = 0.0, aspect_rad = 0.0;
        sample_slope_aspect(fd_dem, x, y, &dem_win,
                             &elev, &slope_rad, &aspect_rad);

        generate_chain(&Out, drv_out, Fi_out->table, &cat_counter,
                        parent_cat, x, y,
                        elev,
                        slope_rad * 180.0 / M_PI,
                        aspect_rad * 180.0 / M_PI,
                        V, theta_rad, az_math,
                        rho_p, L_m,
                        body, &n_polys);
    }

    Vect_destroy_line_struct(Pts);
    Vect_destroy_cats_struct(Cats);

    db_close_database_shutdown_driver(drv_out);
    db_close_database_shutdown_driver(drv_in);
    Rast_close(fd_dem);

    Vect_build(&Out);
    Vect_close(&In);
    Vect_close(&Out);

    G_message(_("Impactor synthesis: %d points read, %d crater polygons "
                 "written, %d points skipped."),
              n_points, n_polys, n_skipped);
    return EXIT_SUCCESS;
}
