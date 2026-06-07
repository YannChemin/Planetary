/*!
 * \file p_shapemodel.c
 *
 * \brief Planetary library - shape models (implementation).
 *
 * All geometry is implemented natively in C99 without SPICE or Qt.
 * Algorithms ported from ISIS3 ShapeModel / EllipsoidShape (USGS, CC0-1.0).
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_shapemodel.h"

#include <math.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_SHAPEMODEL_STANDALONE
#  include <stdlib.h>
#  include <stdarg.h>
#  include <stdio.h>
static void *G_malloc(size_t n) { return malloc(n); }
static void  G_free(void *p)    { free(p); }
static void  G_warning(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "WARNING: "); vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
}
#  define _(s) (s)
#else
#  include <grass/gis.h>
#  include <grass/glocale.h>
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

/* Default delta for DEM normal estimation: 1/1024 degree (~100 m at Mars). */
#define DEM_NORMAL_DELTA_DEG (1.0 / 1024.0)

/* Maximum iterations for DEM ray refinement. */
#define DEM_MAX_ITER 20

/* Convergence threshold for DEM iteration [km]. */
#define DEM_CONV_KM  1.0e-6

/* ================================================================== */
/* Internal struct                                                      */
/* ================================================================== */

struct PShapeModel {
    PShapeType     type;
    /* Ellipsoid / reference radii (all in km) */
    double a, b, c;
    /* DEM callback */
    PShapeRadiusFn dem_fn;
    void          *dem_userdata;
    double         dem_scale_deg;
};

/* ================================================================== */
/* Vector helpers (inline, no header pollution)                         */
/* ================================================================== */

static inline void vec3_sub(const double a[3], const double b[3], double r[3])
{ r[0]=a[0]-b[0]; r[1]=a[1]-b[1]; r[2]=a[2]-b[2]; }

static inline double vec3_dot(const double a[3], const double b[3])
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static inline double vec3_norm(const double a[3])
{ return sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]); }

static inline void vec3_normalize(double a[3])
{
    double n = vec3_norm(a);
    if (n > 0.0) { a[0]/=n; a[1]/=n; a[2]/=n; }
}

static inline void vec3_cross(const double a[3], const double b[3], double r[3])
{
    r[0] = a[1]*b[2] - a[2]*b[1];
    r[1] = a[2]*b[0] - a[0]*b[2];
    r[2] = a[0]*b[1] - a[1]*b[0];
}

static inline void vec3_copy(const double src[3], double dst[3])
{ dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; }

static inline void vec3_scale(double a[3], double s)
{ a[0]*=s; a[1]*=s; a[2]*=s; }

/* ================================================================== */
/* Coordinate conversions                                               */
/* ================================================================== */

void p_shape_xyz_to_latlon(const double pt[3],
                             double *lat_deg, double *lon_deg,
                             double *radius_km)
{
    double r = vec3_norm(pt);
    if (radius_km) *radius_km = r;
    if (r < 1.0e-30) { if(lat_deg)*lat_deg=0; if(lon_deg)*lon_deg=0; return; }
    if (lat_deg) *lat_deg = asin(pt[2] / r) * RAD2DEG;
    if (lon_deg) {
        double lon = atan2(pt[1], pt[0]) * RAD2DEG;
        *lon_deg = (lon < 0.0) ? lon + 360.0 : lon;
    }
}

void p_shape_latlon_to_xyz(double lat_deg, double lon_deg,
                             double radius_km, double pt[3])
{
    double rlat = lat_deg * DEG2RAD;
    double rlon = lon_deg * DEG2RAD;
    pt[0] = radius_km * cos(rlat) * cos(rlon);
    pt[1] = radius_km * cos(rlat) * sin(rlon);
    pt[2] = radius_km * sin(rlat);
}

/* ================================================================== */
/* Ellipsoid local radius (closed form from ISIS3 EllipsoidShape)       */
/* ================================================================== */

static double ellipsoid_local_radius(double a, double b, double c,
                                      double lat_deg, double lon_deg)
{
    double rlat = lat_deg * DEG2RAD;
    double rlon = lon_deg * DEG2RAD;
    double cl = cos(rlat), sl = sin(rlat);
    double cln = cos(rlon), sln = sin(rlon);
    /* xyradius = a*b / sqrt((b*cos(lon))^2 + (a*sin(lon))^2) */
    double den_xy = sqrt((b*cln)*(b*cln) + (a*sln)*(a*sln));
    if (den_xy < 1.0e-30) return c;
    double xy = a * b / den_xy;
    /* radius = xy * c / sqrt((c*cos(lat))^2 + (xy*sin(lat))^2) */
    double den_r = sqrt((c*cl)*(c*cl) + (xy*sl)*(xy*sl));
    if (den_r < 1.0e-30) return 0.0;
    return xy * c / den_r;
}

/* ================================================================== */
/* Ray–ellipsoid intersection                                           */
/*                                                                      */
/* Implements the analytic solution for:                                */
/*   (Ox + t*Dx)^2/a^2 + (Oy + t*Dy)^2/b^2 + (Oz + t*Dz)^2/c^2 = 1  */
/* A*t^2 + B*t + C = 0, smallest positive root.                        */
/* This matches the NAIF surfpt_c algorithm used in ISIS3.              */
/* ================================================================== */

static int ray_ellipsoid(double a, double b, double c,
                          const double obs[3], const double dir[3],
                          double pt[3])
{
    double ia2 = 1.0/(a*a), ib2 = 1.0/(b*b), ic2 = 1.0/(c*c);

    double A = dir[0]*dir[0]*ia2 + dir[1]*dir[1]*ib2 + dir[2]*dir[2]*ic2;
    double B = 2.0*(obs[0]*dir[0]*ia2 + obs[1]*dir[1]*ib2 + obs[2]*dir[2]*ic2);
    double C = obs[0]*obs[0]*ia2 + obs[1]*obs[1]*ib2 + obs[2]*obs[2]*ic2 - 1.0;

    if (A < 1.0e-30) return 0;

    double disc = B*B - 4.0*A*C;
    if (disc < 0.0) return 0;   /* ray misses ellipsoid */

    double sq = sqrt(disc);
    double t1 = (-B - sq) / (2.0*A);
    double t2 = (-B + sq) / (2.0*A);

    /* Select the nearest positive t (front intersection). */
    double t = -1.0;
    if (t1 > 1.0e-10 && t2 > 1.0e-10) t = (t1 < t2) ? t1 : t2;
    else if (t1 > 1.0e-10) t = t1;
    else if (t2 > 1.0e-10) t = t2;

    if (t < 0.0) return 0;

    pt[0] = obs[0] + t*dir[0];
    pt[1] = obs[1] + t*dir[1];
    pt[2] = obs[2] + t*dir[2];
    return 1;
}

/* ================================================================== */
/* Ellipsoid surface normal (analytic gradient / |gradient|)            */
/* n = (x/a^2, y/b^2, z/c^2) normalised                               */
/* ================================================================== */

static void ellipsoid_normal(double a, double b, double c,
                              const double pt[3], double n[3])
{
    n[0] = pt[0] / (a*a);
    n[1] = pt[1] / (b*b);
    n[2] = pt[2] / (c*c);
    vec3_normalize(n);
}

/* ================================================================== */
/* DEM iteration: refine ellipsoid seed to lie on the DEM surface       */
/* ================================================================== */

static int dem_intersect_ray(const PShapeModel *m,
                              const double obs[3], const double dir[3],
                              double pt[3])
{
    /* 1. Ellipsoid seed */
    if (!ray_ellipsoid(m->a, m->b, m->c, obs, dir, pt)) return 0;

    /* 2. Iterative DEM refinement */
    for (int iter = 0; iter < DEM_MAX_ITER; iter++) {
        double lat, lon, r_ell;
        p_shape_xyz_to_latlon(pt, &lat, &lon, &r_ell);

        double r_dem = m->dem_fn(lat, lon, m->dem_userdata);
        if (r_dem <= 0.0) break;  /* no-data: stop with current point */

        double delta = r_dem - r_ell;
        if (fabs(delta) < DEM_CONV_KM) break;  /* converged */

        /* Move along the look direction by delta/cos(angle) ≈ delta.
         * More robustly: scale the current point radially. */
        double scale = r_dem / r_ell;
        pt[0] *= scale;
        pt[1] *= scale;
        pt[2] *= scale;
    }
    return 1;
}

/* ================================================================== */
/* DEM surface normal via 4-neighbour finite difference                 */
/* ================================================================== */

static void dem_normal(const PShapeModel *m, const double pt[3], double n[3])
{
    double lat, lon, r_c;
    p_shape_xyz_to_latlon(pt, &lat, &lon, &r_c);

    double d = (m->dem_scale_deg > 0.0)
               ? m->dem_scale_deg : DEM_NORMAL_DELTA_DEG;

    /* Four cardinal neighbour points in lat/lon space. */
    double lat_n = lat + d, lat_s = lat - d;
    double lon_e = lon + d, lon_w = lon - d;

    /* Clamp poles. */
    if (lat_n >  90.0) lat_n =  90.0;
    if (lat_s < -90.0) lat_s = -90.0;

    /* Wrap longitude. */
    if (lon_e >= 360.0) lon_e -= 360.0;
    if (lon_w <    0.0) lon_w += 360.0;

    /* Get DEM radii at neighbours (fall back to ellipsoid if no data). */
    double r_n = m->dem_fn(lat_n, lon,   m->dem_userdata);
    double r_s = m->dem_fn(lat_s, lon,   m->dem_userdata);
    double r_e = m->dem_fn(lat,   lon_e, m->dem_userdata);
    double r_w = m->dem_fn(lat,   lon_w, m->dem_userdata);

    if (r_n <= 0.0) r_n = ellipsoid_local_radius(m->a, m->b, m->c, lat_n, lon);
    if (r_s <= 0.0) r_s = ellipsoid_local_radius(m->a, m->b, m->c, lat_s, lon);
    if (r_e <= 0.0) r_e = ellipsoid_local_radius(m->a, m->b, m->c, lat,   lon_e);
    if (r_w <= 0.0) r_w = ellipsoid_local_radius(m->a, m->b, m->c, lat,   lon_w);

    /* Convert to XYZ. */
    double pn[3], ps[3], pe[3], pw[3];
    p_shape_latlon_to_xyz(lat_n, lon,   r_n, pn);
    p_shape_latlon_to_xyz(lat_s, lon,   r_s, ps);
    p_shape_latlon_to_xyz(lat,   lon_e, r_e, pe);
    p_shape_latlon_to_xyz(lat,   lon_w, r_w, pw);

    /* Two tangent vectors (N−S and E−W), cross product gives normal. */
    double t_ns[3], t_ew[3];
    vec3_sub(pn, ps, t_ns);
    vec3_sub(pe, pw, t_ew);
    vec3_cross(t_ew, t_ns, n);
    vec3_normalize(n);

    /* Ensure normal points outward (same hemisphere as point). */
    if (vec3_dot(n, pt) < 0.0) {
        n[0]=-n[0]; n[1]=-n[1]; n[2]=-n[2];
    }
}

/* ================================================================== */
/* Construction                                                         */
/* ================================================================== */

PShapeModel *p_shape_ellipsoid(double a_km, double b_km, double c_km)
{
    if (a_km <= 0.0 || b_km <= 0.0 || c_km <= 0.0) {
        G_warning(_("p_shape_ellipsoid: radii must be positive (got %g %g %g)"),
                   a_km, b_km, c_km);
        return NULL;
    }
    struct PShapeModel *m = (struct PShapeModel *)G_malloc(sizeof(*m));
    m->type = P_SHAPE_ELLIPSOID;
    m->a = a_km; m->b = b_km; m->c = c_km;
    m->dem_fn = NULL; m->dem_userdata = NULL; m->dem_scale_deg = 0.0;
    return (PShapeModel *)m;
}

PShapeModel *p_shape_sphere(double radius_km)
{
    return p_shape_ellipsoid(radius_km, radius_km, radius_km);
}

PShapeModel *p_shape_dem(PShapeRadiusFn fn, void *userdata,
                          double a_km, double b_km, double c_km,
                          double dem_scale_deg)
{
    if (!fn) {
        G_warning(_("p_shape_dem: radius callback must not be NULL"));
        return NULL;
    }
    if (a_km <= 0.0 || b_km <= 0.0 || c_km <= 0.0) {
        G_warning(_("p_shape_dem: reference ellipsoid radii must be positive"));
        return NULL;
    }
    struct PShapeModel *m = (struct PShapeModel *)G_malloc(sizeof(*m));
    m->type = P_SHAPE_DEM;
    m->a = a_km; m->b = b_km; m->c = c_km;
    m->dem_fn        = fn;
    m->dem_userdata  = userdata;
    m->dem_scale_deg = dem_scale_deg;
    return (PShapeModel *)m;
}

PShapeModel *p_shape_plane(void)
{
    struct PShapeModel *m = (struct PShapeModel *)G_malloc(sizeof(*m));
    m->type = P_SHAPE_PLANE;
    m->a = m->b = m->c = 0.0;
    m->dem_fn = NULL; m->dem_userdata = NULL; m->dem_scale_deg = 0.0;
    return (PShapeModel *)m;
}

/* ================================================================== */
/* Core geometric operations                                            */
/* ================================================================== */

int p_shape_intersect_ray(const PShapeModel *m,
                           const double obs[3], const double dir[3],
                           double pt[3])
{
    const struct PShapeModel *sm = (const struct PShapeModel *)m;
    if (!sm) return 0;

    switch (sm->type) {

    case P_SHAPE_ELLIPSOID:
        return ray_ellipsoid(sm->a, sm->b, sm->c, obs, dir, pt);

    case P_SHAPE_DEM:
        return dem_intersect_ray(sm, obs, dir, pt);

    case P_SHAPE_PLANE: {
        /* z = 0 plane: t = -obs[2] / dir[2] */
        if (fabs(dir[2]) < 1.0e-15) return 0;
        double t = -obs[2] / dir[2];
        if (t < 1.0e-10) return 0;
        pt[0] = obs[0] + t*dir[0];
        pt[1] = obs[1] + t*dir[1];
        pt[2] = 0.0;
        return 1;
    }

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */

void p_shape_normal(const PShapeModel *m,
                     const double pt[3], double normal[3])
{
    const struct PShapeModel *sm = (const struct PShapeModel *)m;
    if (!sm) { normal[0]=0; normal[1]=0; normal[2]=1; return; }

    switch (sm->type) {

    case P_SHAPE_ELLIPSOID:
        ellipsoid_normal(sm->a, sm->b, sm->c, pt, normal);
        break;

    case P_SHAPE_DEM:
        dem_normal(sm, pt, normal);
        break;

    case P_SHAPE_PLANE:
        normal[0] = 0.0;
        normal[1] = 0.0;
        normal[2] = (pt[2] >= 0.0) ? 1.0 : -1.0;
        break;

    default:
        normal[0] = 0.0; normal[1] = 0.0; normal[2] = 1.0;
    }
}

/* ------------------------------------------------------------------ */

double p_shape_local_radius_km(const PShapeModel *m,
                                double lat_deg, double lon_deg)
{
    const struct PShapeModel *sm = (const struct PShapeModel *)m;
    if (!sm) return 0.0;

    switch (sm->type) {
    case P_SHAPE_ELLIPSOID:
        return ellipsoid_local_radius(sm->a, sm->b, sm->c, lat_deg, lon_deg);
    case P_SHAPE_DEM: {
        double r = sm->dem_fn(lat_deg, lon_deg, sm->dem_userdata);
        if (r <= 0.0)
            return ellipsoid_local_radius(sm->a, sm->b, sm->c, lat_deg, lon_deg);
        return r;
    }
    case P_SHAPE_PLANE:
        return 0.0;
    default:
        return 0.0;
    }
}

/* ================================================================== */
/* Angle calculations                                                   */
/* ================================================================== */

/*
 * Common kernel: angle between two unit vectors computed at surface point.
 * dir1 = normalize(pos1 - pt)
 * dir2 = normalize(pos2 - pt)   (pass NULL to use normal directly)
 */
static double angle_between(const double pt[3],
                              const double vec1[3],
                              const double vec2[3])
{
    double d1[3], d2[3];
    vec3_sub(vec1, pt, d1); vec3_normalize(d1);
    vec3_sub(vec2, pt, d2); vec3_normalize(d2);
    double c = vec3_dot(d1, d2);
    if (c >  1.0) return 0.0;
    if (c < -1.0) return 180.0;
    return acos(c) * RAD2DEG;
}

static double angle_normal_vec(const double pt[3],
                                const double normal[3],
                                const double pos[3])
{
    double d[3];
    vec3_sub(pos, pt, d); vec3_normalize(d);
    double c = vec3_dot(normal, d);
    if (c >  1.0) return 0.0;
    if (c < -1.0) return 180.0;
    return acos(c) * RAD2DEG;
}

double p_shape_incidence_angle(const double pt[3],
                                const double normal[3],
                                const double sun[3])
{
    return angle_normal_vec(pt, normal, sun);
}

double p_shape_emission_angle(const double pt[3],
                               const double normal[3],
                               const double obs[3])
{
    return angle_normal_vec(pt, normal, obs);
}

double p_shape_phase_angle(const double pt[3],
                            const double obs[3],
                            const double sun[3])
{
    return angle_between(pt, obs, sun);
}

/* ================================================================== */
/* Row-processing with OpenMP                                           */
/* ================================================================== */

void p_shape_apply_row(const PShapeModel *m,
                        int nsamples,
                        const double obs[3],
                        const double *dirs,
                        const double sun[3],
                        PShapeResult *out)
{
    int s;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        const double *dir = dirs + 3*s;
        double pt[3], normal[3];

        if (!p_shape_intersect_ray(m, obs, dir, pt)) {
            /* Ray misses: signal NaN. */
            out[s].incidence    = NAN;
            out[s].emission     = NAN;
            out[s].phase        = NAN;
            out[s].local_radius = NAN;
            out[s].lat          = NAN;
            out[s].lon          = NAN;
            continue;
        }

        p_shape_normal(m, pt, normal);

        out[s].incidence = p_shape_incidence_angle(pt, normal, sun);
        out[s].emission  = p_shape_emission_angle (pt, normal, obs);
        out[s].phase     = p_shape_phase_angle    (pt, obs, sun);

        double lat, lon, r;
        p_shape_xyz_to_latlon(pt, &lat, &lon, &r);
        out[s].lat = lat;
        out[s].lon = lon;
        out[s].local_radius = p_shape_local_radius_km(m, lat, lon);
    }
}

/* ================================================================== */
/* Utilities                                                            */
/* ================================================================== */

const char *p_shape_name(const PShapeModel *m)
{
    if (!m) return "NULL";
    switch (((const struct PShapeModel *)m)->type) {
    case P_SHAPE_ELLIPSOID: return "Ellipsoid";
    case P_SHAPE_DEM:       return "DEM";
    case P_SHAPE_PLANE:     return "Plane";
    default:                return "Unknown";
    }
}

void p_shape_free(PShapeModel *m)
{
    if (m) G_free(m);
}
