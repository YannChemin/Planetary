/****************************************************************************
 * MODULE:       p.crater.draw (nms_output.c)
 * PURPOSE:      Non-maximum suppression and final vector polygon output.
 *
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/vector.h>
#include <grass/dbmi.h>
#include <grass/glocale.h>

#include "p_crater_draw.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POLY_VERTS 48

/* ------------------------------------------------------------------ */
void cl_init(CandidateList *cl) { cl->data = NULL; cl->n = 0; cl->cap = 0; }
void cl_free(CandidateList *cl) { G_free(cl->data); cl->data = NULL; cl->n = cl->cap = 0; }

void cl_push(CandidateList *cl, const CraterCandidate *c)
{
    if (cl->n >= cl->cap) {
        cl->cap = cl->cap ? cl->cap * 2 : 256;
        cl->data = G_realloc(cl->data, (size_t)cl->cap * sizeof(*cl->data));
    }
    cl->data[cl->n++] = *c;
}

/* ------------------------------------------------------------------ */
/* IoU between two disks of (cx, cy, r). Uses the closed-form circle */
/* intersection area.                                                 */
/* ------------------------------------------------------------------ */
static double disk_iou(const CraterCandidate *a, const CraterCandidate *b)
{
    double dx = a->cx - b->cx, dy = a->cy - b->cy;
    double d  = sqrt(dx * dx + dy * dy);
    double ra = a->radius_m, rb = b->radius_m;
    if (d >= ra + rb)             return 0.0;
    if (d + fmin(ra, rb) <= fmax(ra, rb)) {
        double small = M_PI * fmin(ra, rb) * fmin(ra, rb);
        double big   = M_PI * fmax(ra, rb) * fmax(ra, rb);
        return small / big;
    }
    double ra2 = ra * ra, rb2 = rb * rb;
    double alpha = acos((d * d + ra2 - rb2) / (2.0 * d * ra)) * 2.0;
    double beta  = acos((d * d + rb2 - ra2) / (2.0 * d * rb)) * 2.0;
    double inter = 0.5 * ra2 * (alpha - sin(alpha))
                 + 0.5 * rb2 * (beta  - sin(beta));
    double uni   = M_PI * (ra2 + rb2) - inter;
    return inter / uni;
}

/* Simple stable sort by descending confidence (qsort).               */
static int cmp_conf(const void *p, const void *q)
{
    const CraterCandidate *a = p, *b = q;
    if (a->confidence < b->confidence) return  1;
    if (a->confidence > b->confidence) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Multi-ring basin aggregation.                                        */
/* ------------------------------------------------------------------ */
int aggregate_multiring(CandidateList *cl,
                         double centre_tol_frac,
                         double min_radius_ratio)
{
    if (cl->n < 2) return 0;
    /* Reset prior tags. */
    for (int i = 0; i < cl->n; i++) {
        cl->data[i].basin_id   = 0;
        cl->data[i].ring_index = 0;
    }
    int next_basin = 1;
    int *idx = G_malloc((size_t)cl->n * sizeof(int));

    for (int i = 0; i < cl->n; i++) {
        if (cl->data[i].basin_id) continue;

        /* Build the candidate-set for a hypothetical basin centred
         * on i: every candidate j whose centre is within tolerance
         * of i AND whose radius is not a near-duplicate of any
         * already-collected ring in this group.                     */
        int k = 0;
        idx[k++] = i;
        for (int j = 0; j < cl->n; j++) {
            if (j == i || cl->data[j].basin_id) continue;
            double dx = cl->data[i].cx - cl->data[j].cx;
            double dy = cl->data[i].cy - cl->data[j].cy;
            double d  = sqrt(dx * dx + dy * dy);
            double rmin = fmin(cl->data[i].radius_m,
                                cl->data[j].radius_m);
            if (d > centre_tol_frac * rmin) continue;

            /* radius dissimilarity test vs all rings collected so far. */
            int distinct = 1;
            for (int m = 0; m < k; m++) {
                double rl = fmin(cl->data[idx[m]].radius_m,
                                  cl->data[j].radius_m);
                double rh = fmax(cl->data[idx[m]].radius_m,
                                  cl->data[j].radius_m);
                if (rl <= 0.0 || (rh / rl) < min_radius_ratio) {
                    distinct = 0; break;
                }
            }
            if (distinct) idx[k++] = j;
        }
        if (k < 2) continue;   /* need 2+ rings to call it a basin   */

        /* Sort the collected radii ascending so ring_index 1 = inner. */
        for (int a = 0; a < k - 1; a++)
            for (int b = a + 1; b < k; b++)
                if (cl->data[idx[a]].radius_m >
                    cl->data[idx[b]].radius_m) {
                    int tmp = idx[a]; idx[a] = idx[b]; idx[b] = tmp;
                }
        for (int r = 0; r < k; r++) {
            cl->data[idx[r]].basin_id   = next_basin;
            cl->data[idx[r]].ring_index = r + 1;
        }
        next_basin++;
    }
    G_free(idx);
    int n_basins = next_basin - 1;
    G_message(_("Multi-ring aggregation: %d basin(s) detected "
                 "(centre tol = %.2f * r_min, radius ratio >= %.2f)."),
              n_basins, centre_tol_frac, min_radius_ratio);
    return n_basins;
}

void apply_nms(CandidateList *cl, double iou_threshold)
{
    if (cl->n == 0) return;
    qsort(cl->data, cl->n, sizeof(*cl->data), cmp_conf);

    char *kept = G_calloc((size_t)cl->n, 1);
    int n_keep = 0;
    for (int i = 0; i < cl->n; i++) {
        kept[i] = 1;
        for (int j = 0; j < i; j++) {
            if (!kept[j]) continue;
            if (disk_iou(&cl->data[i], &cl->data[j]) >= iou_threshold) {
                /* Suppressed by an earlier (higher-confidence) cand.
                 * If methods differ, mark the survivor as merged.   */
                if (strcmp(cl->data[i].method,
                            cl->data[j].method) != 0) {
                    strncpy(cl->data[j].method, "merged",
                             sizeof(cl->data[j].method));
                    cl->data[j].n_methods++;
                }
                kept[i] = 0;
                break;
            }
        }
        if (kept[i]) n_keep++;
    }

    /* Compact in place. */
    int w = 0;
    for (int i = 0; i < cl->n; i++) {
        if (kept[i]) cl->data[w++] = cl->data[i];
    }
    cl->n = w;
    G_free(kept);
    G_message(_("NMS kept %d candidates."), cl->n);
}

/* ------------------------------------------------------------------ */
/* Write the final polygons. 48-vertex circles, attributes ready for
 * p.crater and p.crater.freq consumption.                             */
/* ------------------------------------------------------------------ */
static const char *kOutColumns =
    "cat INTEGER PRIMARY KEY,"
    "cx DOUBLE PRECISION,"
    "cy DOUBLE PRECISION,"
    "D_eq DOUBLE PRECISION,"
    "axis_ratio DOUBLE PRECISION,"
    "azimuth_deg DOUBLE PRECISION,"
    "confidence DOUBLE PRECISION,"
    "method VARCHAR(16),"
    "n_methods INTEGER,"
    "dD_simple DOUBLE PRECISION,"
    "basin_id INTEGER,"
    "ring_index INTEGER";

static void exec_sql(dbDriver *driver, const char *sql)
{
    dbString stmt;
    db_init_string(&stmt);
    db_set_string(&stmt, sql);
    if (db_execute_immediate(driver, &stmt) != DB_OK)
        G_warning(_("SQL failed: %s"), sql);
    db_free_string(&stmt);
}

int write_candidates_vector(const char *out_name, const CandidateList *cl)
{
    struct Map_info Out;
    if (Vect_open_new(&Out, out_name, 0) < 0)
        G_fatal_error(_("Cannot create vector <%s>"), out_name);
    Vect_hist_command(&Out);

    struct field_info *Fi = Vect_default_field_info(&Out, 1, NULL, GV_1TABLE);
    Vect_map_add_dblink(&Out, 1, NULL, Fi->table, GV_KEY_COLUMN,
                          Fi->database, Fi->driver);
    dbDriver *drv = db_start_driver_open_database(Fi->driver, Fi->database);
    if (!drv) G_fatal_error(_("Cannot open output db"));
    db_set_error_handler_driver(drv);

    char sql[2048];
    snprintf(sql, sizeof(sql), "CREATE TABLE %s (%s)", Fi->table, kOutColumns);
    exec_sql(drv, sql);

    struct line_pnts *Pts = Vect_new_line_struct();
    struct line_cats *Cats = Vect_new_cats_struct();

    for (int i = 0; i < cl->n; i++) {
        const CraterCandidate *c = &cl->data[i];
        int cat = i + 1;

        /* 48-vertex closed ring, bit-exact closure (same trick as
         * impactor_points to satisfy GRASS topology).               */
        Vect_reset_line(Pts);
        Vect_reset_cats(Cats);
        for (int v = 0; v < POLY_VERTS; v++) {
            double t = 2.0 * M_PI * v / POLY_VERTS;
            double wx = c->cx + c->radius_m * cos(t);
            double wy = c->cy + c->radius_m * sin(t);
            Vect_append_point(Pts, wx, wy, 0.0);
        }
        Vect_append_point(Pts, Pts->x[0], Pts->y[0], 0.0);
        Vect_write_line(&Out, GV_BOUNDARY, Pts, Cats);

        Vect_reset_line(Pts);
        Vect_reset_cats(Cats);
        Vect_append_point(Pts, c->cx, c->cy, 0.0);
        Vect_cat_set(Cats, 1, cat);
        Vect_write_line(&Out, GV_CENTROID, Pts, Cats);

        /* dD_simple: NULL when unset; basin_id/ring_index: NULL when 0. */
        char dd_part[64], basin_part[64];
        if (c->dD_simple > 0.0)
            snprintf(dd_part, sizeof(dd_part), "%.6g", c->dD_simple);
        else
            snprintf(dd_part, sizeof(dd_part), "NULL");
        if (c->basin_id > 0)
            snprintf(basin_part, sizeof(basin_part), "%d,%d",
                     c->basin_id, c->ring_index);
        else
            snprintf(basin_part, sizeof(basin_part), "NULL,NULL");

        snprintf(sql, sizeof(sql),
                 "INSERT INTO %s (cat,cx,cy,D_eq,axis_ratio,azimuth_deg,"
                 "confidence,method,n_methods,dD_simple,basin_id,"
                 "ring_index) VALUES "
                 "(%d,%.6g,%.6g,%.6g,1.0,0.0,%.6g,'%s',%d,%s,%s)",
                 Fi->table, cat, c->cx, c->cy, 2.0 * c->radius_m,
                 c->confidence, c->method, c->n_methods, dd_part,
                 basin_part);
        exec_sql(drv, sql);
    }

    Vect_destroy_line_struct(Pts);
    Vect_destroy_cats_struct(Cats);
    db_close_database_shutdown_driver(drv);
    Vect_build(&Out);
    Vect_close(&Out);

    G_message(_("Wrote %d crater polygons to <%s>."), cl->n, out_name);
    return 0;
}
