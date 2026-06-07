/*!
 * \file p_projection_planet.c
 *
 * \brief Planetary library - projection types absent from PROJ (implementation).
 *
 * Algorithms ported from ISIS3 (USGS Astrogeology, CC0-1.0):
 *   RingCylindrical, LunarAzimuthalEqualArea, UpturnedEllipsoidTransverseAzimuthal.
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_projection_planet.h"

#include <math.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_PROJ_PLANET_STANDALONE
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
#define HALFPI  (M_PI / 2.0)
#define TWOPI   (2.0 * M_PI)
#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

/* ================================================================== */
/* Internal concrete struct                                             */
/* ================================================================== */

struct PProjPlanet {
    PProjPlanetType type;
    PProjPlanetParams params;

    /* UpturnedTA precomputed constants (from ISIS3 init()). */
    double ta_lambda0; /* centre longitude [rad, positive-east]      */
    double ta_a;       /* semi-major axis                             */
    double ta_b;       /* semi-minor axis                             */
    double ta_e;       /* eccentricity sqrt(1-(b/a)^2)                */
    double ta_t;       /* (b/a)^2 = 1 - e^2                          */
    double ta_t1;      /* e * b/a = e * sqrt(1-e^2)                  */
    double ta_k;       /* scale factor: 2*a*t*exp(t1*atan(t1))       */
};

/* ================================================================== */
/* Helper: clamp to [-1, 1] for safe asin/acos                         */
/* ================================================================== */
static inline double clamp11(double x)
{ return x >  1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); }

/* ================================================================== */
/* RingCylindrical forward / inverse                                    */
/* ================================================================== */

/*
 * ISIS3 RingCylindrical::SetGround():
 *   delta_az = ring_lon_rad - center_lon_rad   [adjusted for direction]
 *   x = delta_az * center_radius
 *   y = center_radius - ring_radius
 *
 * ISIS3 RingCylindrical::SetCoordinate():
 *   ring_radius  = center_radius - y
 *   ring_lon_rad = center_lon_rad + x / center_radius
 */

static int ring_cyl_fwd(const struct PProjPlanet *p,
                          double ring_radius, double ring_lon_deg,
                          double *x, double *y)
{
    const PProjRingCyl *rc = &p->params.ring_cyl;
    if (ring_radius <= 0.0) return 0;

    double center_lon_rad = rc->center_lon_deg * DEG2RAD;
    double ring_lon_rad   = ring_lon_deg        * DEG2RAD;

    if (rc->clockwise_lon) {
        center_lon_rad *= -1.0;
        ring_lon_rad   *= -1.0;
    }

    double delta = ring_lon_rad - center_lon_rad;
    *x = delta * rc->center_radius;
    *y = rc->center_radius - ring_radius;
    return 1;
}

static int ring_cyl_inv(const struct PProjPlanet *p,
                          double x, double y,
                          double *ring_radius, double *ring_lon_deg)
{
    const PProjRingCyl *rc = &p->params.ring_cyl;

    *ring_radius = rc->center_radius - y;
    if (*ring_radius <= 0.0) return 0;

    double center_lon_rad = rc->center_lon_deg * DEG2RAD;
    if (rc->clockwise_lon) center_lon_rad *= -1.0;

    double ring_lon_rad = center_lon_rad + x / rc->center_radius;
    if (rc->clockwise_lon) ring_lon_rad *= -1.0;

    *ring_lon_deg = ring_lon_rad * RAD2DEG;
    return 1;
}

/* ================================================================== */
/* LunarAzimuthalEqualArea forward / inverse                           */
/* ================================================================== */

/*
 * ISIS3 LunarAzimuthalEqualArea::SetGround() (planetographic lat assumed):
 *   E = acos(cos(lat_rad) * cos(lon_rad))
 *   D = pi/2 - asin(sin(lon_rad)*cos(lat_rad)/sin(E));  sign(D)=sign(lat)
 *   PFAC = (pi/2 + max_lib_rad) / (pi/2)
 *   RP = R * sin(E / PFAC)
 *   x = RP * cos(D);  y = RP * sin(D)
 *
 * ISIS3 LunarAzimuthalEqualArea::SetCoordinate():
 *   RP = sqrt(x^2 + y^2)
 *   D  = atan2(y, x)
 *   E  = PFAC * asin(RP / R)
 *   lat = pi/2 - acos(sin(D)*sin(E))
 *   lon = asin(sin(E)*cos(D) / sin(pi/2 - lat))
 *   if E >= pi/2: lon = sign(lon)*(pi - |lon|)
 */

static int lunar_ea_fwd(const struct PProjPlanet *p,
                          double lat_deg, double lon_deg,
                          double *x, double *y)
{
    const PProjLunarAzimuthalEA *la = &p->params.lunar_ea;
    double R   = la->equatorial_radius;
    double maxL = la->max_libration_deg * DEG2RAD;

    double lat_rad = lat_deg * DEG2RAD;
    double lon_rad = lon_deg * DEG2RAD;

    if (lon_rad == 0.0 && lat_rad == 0.0) { *x = 0.0; *y = 0.0; return 1; }

    double E = acos(clamp11(cos(lat_rad) * cos(lon_rad)));

    double sin_E = sin(E);
    double D;
    if (fabs(sin_E) < 1.0e-15) {
        D = 0.0;
    } else {
        double t = sin(lon_rad) * cos(lat_rad) / sin_E;
        D = HALFPI - asin(clamp11(t));
        if (lat_rad < 0.0) D = -D;
    }

    double PFAC = (HALFPI + maxL) / HALFPI;
    double RP   = R * sin(E / PFAC);

    *x = RP * cos(D);
    *y = RP * sin(D);
    return 1;
}

static int lunar_ea_inv(const struct PProjPlanet *p,
                          double x, double y,
                          double *lat_deg, double *lon_deg)
{
    const PProjLunarAzimuthalEA *la = &p->params.lunar_ea;
    double R    = la->equatorial_radius;
    double maxL = la->max_libration_deg * DEG2RAD;

    if (x == 0.0 && y == 0.0) { *lat_deg = 0.0; *lon_deg = 0.0; return 1; }

    double RP   = sqrt(x*x + y*y);
    double D    = atan2(y, x);
    double PFAC = (HALFPI + maxL) / HALFPI;

    double t = RP / R;
    if (fabs(t) > 1.0) return 0;  /* outside projection boundary */

    double E = PFAC * asin(clamp11(t));

    double lat = HALFPI - acos(clamp11(sin(D) * sin(E)));

    double lon;
    const double EPS = 1.0e-10;
    if (fabs(HALFPI - fabs(lat)) <= EPS) {
        lon = 0.0;
    } else {
        /* ISIS3: sin(HALFPI - lat) = cos(lat) — NOT cos(HALFPI - lat) */
        double cos_lat = cos(lat);
        if (fabs(cos_lat) < EPS) return 0;
        double s = sin(E) * cos(D) / cos_lat;
        lon = asin(clamp11(s));
    }

    if (E >= HALFPI) {
        lon = (lon <= 0.0) ? (-M_PI - lon) : (M_PI - lon);
    }

    *lat_deg = lat * RAD2DEG;
    *lon_deg = lon * RAD2DEG;
    return 1;
}

/* ================================================================== */
/* UpturnedEllipsoidTransverseAzimuthal forward / inverse              */
/* ================================================================== */

/* Compute (x,y) for one case: cosz > 0.5 (i.e. z < pi/3). */
static void ueta_fwd_near(const struct PProjPlanet *p,
                            double phiNorm, double lambdaNorm,
                            double cosz, double *x, double *y)
{
    double sinz = sqrt(fmax(0.0, 1.0 - cosz*cosz));
    double phi  = HALFPI - atan2(sinz, p->ta_t * cosz);
    double sinP = sin(phi);
    /* rhoOverTanZ = k*sin(phi) / ((1+sin(phi)) * t * exp(t1*atan(t1*sin(phi)))) */
    double denom = (1.0 + sinP) * p->ta_t * exp(p->ta_t1 * atan(p->ta_t1 * sinP));
    if (fabs(denom) < 1.0e-20) { *x = 0.0; *y = 0.0; return; }
    double rhoOtanz = p->ta_k * sinP / denom;
    *x = rhoOtanz * tan(lambdaNorm);
    *y = rhoOtanz * sin(phiNorm) / cosz;
}

/* Compute (x,y) for the far case: cosz <= 0.5. */
static void ueta_fwd_far(const struct PProjPlanet *p,
                           double phiNorm, double lambdaNorm,
                           double cosz, double *x, double *y)
{
    /* Clamp near singularity at cosz == -1 (z == pi). */
    const double tolerance = 0.0016;
    double coszmax = cos(M_PI - tolerance);
    if (cosz < coszmax) {
        cosz = coszmax;
        /* Clamp lambdaNorm away from ±pi to avoid tan singularity. */
        double lmod = fmod(lambdaNorm, TWOPI);
        if      (lmod >  M_PI - tolerance && lmod <=  M_PI) lambdaNorm =  M_PI - tolerance;
        else if (lmod >=  M_PI && lmod <  M_PI + tolerance) lambdaNorm =  M_PI + tolerance;
        else if (lmod > -M_PI - tolerance && lmod <= -M_PI) lambdaNorm = -M_PI - tolerance;
        else if (lmod >= -M_PI && lmod < -M_PI + tolerance) lambdaNorm = -M_PI + tolerance;
    }

    double sinz = sqrt(fmax(0.0, 1.0 - cosz*cosz));

    /* phi = arctan( t * cos(z) / sin(z) ) — note sign: upturned ellipsoid */
    double phi = atan2(p->ta_t * cosz, sinz);
    /* NOTE: when cosz < 0 (z > pi/2), phi can be negative, which is correct. */

    double sinP = sin(phi);
    double denom = (1.0 + sinP) * p->ta_t * exp(p->ta_t1 * atan(p->ta_t1 * sinP));
    if (fabs(denom) < 1.0e-20) { *x = 0.0; *y = 0.0; return; }
    double rhoOsinz = p->ta_k * sinP / denom;

    /* x = (rho/sinz) * cos(phiNorm) * sin(lambdaNorm)
     * y = (rho/sinz) * sin(phiNorm) */
    *x = rhoOsinz * cos(phiNorm) * sin(lambdaNorm);
    *y = rhoOsinz * sin(phiNorm);
}

static int ueta_fwd(const struct PProjPlanet *p,
                     double lat_deg, double lon_deg,
                     double *x, double *y)
{
    double phiNorm   = lat_deg * DEG2RAD;  /* planetocentric lat */
    double lambdaNorm = lon_deg * DEG2RAD - p->ta_lambda0;

    double cosz = cos(phiNorm) * cos(lambdaNorm);

    if (cosz >= 1.0) { *x = 0.0; *y = 0.0; return 1; }

    if (cosz > 0.5) {
        ueta_fwd_near(p, phiNorm, lambdaNorm, cosz, x, y);
    } else {
        ueta_fwd_far(p, phiNorm, lambdaNorm, cosz, x, y);
    }
    return 1;
}

static int ueta_inv(const struct PProjPlanet *p,
                     double x, double y,
                     double *lat_deg, double *lon_deg)
{
    if (x == 0.0 && y == 0.0) {
        *lat_deg = 0.0;
        *lon_deg = p->ta_lambda0 * RAD2DEG;
        return 1;
    }

    double rho = sqrt(x*x + y*y);

    /* Newton's method to solve: g(phi) = rho
     * g(phi) = k*cos(phi) / [(1+sin(phi))*exp(t1*atan(t1*sin(phi)))]
     * g'(phi) = -k*(1+t1^2) / [(1+sin(phi))*exp(...) * (1+t1^2*sin^2(phi))]
     */
    double phi = 0.0;  /* start at equator */
    const double TOL = 1.0e-10;
    int converged = 0;
    for (int it = 0; it < 1000 && !converged; it++) {
        double sinP  = sin(phi);
        double expT  = exp(p->ta_t1 * atan(p->ta_t1 * sinP));
        double denom = (1.0 + sinP) * expT;
        if (fabs(denom) < 1.0e-20) break;
        double g    = p->ta_k * cos(phi) / denom - rho;
        double gp   = -p->ta_k * (1.0 + p->ta_t1*p->ta_t1)
                      / (denom * (1.0 + p->ta_t1*p->ta_t1 * sinP*sinP));
        if (fabs(gp) < 1.0e-20) break;
        double phi1 = phi - g / gp;
        /* Clamp to [-pi/2, pi/2]. */
        if (phi1 >  HALFPI) phi1 =  HALFPI;
        if (phi1 < -HALFPI) phi1 = -HALFPI;
        if (fabs(phi - phi1) < TOL) converged = 1;
        phi = phi1;
    }
    if (!converged) return 0;

    /* z = atan2(1 - e^2, tan(phi)) = atan2(t, tan(phi)) */
    double z = atan2(p->ta_t, tan(phi));

    /* phiNorm from: y = (rho/sinz) * sin(phiNorm) */
    double sinz = sin(z);
    if (fabs(sinz) < 1.0e-20 || rho < 1.0e-20) return 0;
    double phiNorm = asin(clamp11(y * sinz / rho));

    /* lambdaNorm: cos(z) = cos(phiNorm)*cos(lambdaNorm) */
    double cosLN = cos(z) / fmax(fabs(cos(phiNorm)), 1.0e-20);
    double lambdaNorm;
    if      (cosLN >  1.0) lambdaNorm = 0.0;
    else if (cosLN < -1.0) lambdaNorm = M_PI;
    else                    lambdaNorm = acos(clamp11(cosLN));

    /* Determine sign of lambdaNorm from sign of x. */
    if (x < 0.0) lambdaNorm = -lambdaNorm;

    *lat_deg = phiNorm * RAD2DEG;
    *lon_deg = (lambdaNorm + p->ta_lambda0) * RAD2DEG;
    return 1;
}

/* ================================================================== */
/* Public API — construction                                            */
/* ================================================================== */

PProjPlanet *p_proj_planet_create(PProjPlanetType type,
                                   const PProjPlanetParams *params)
{
    struct PProjPlanet *p = (struct PProjPlanet *)G_malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    p->type = type;

    /* Install defaults then overlay caller params. */
    switch (type) {
    case P_PROJ_RING_CYL:
        p->params.ring_cyl.center_radius  = 105000.0;
        p->params.ring_cyl.center_lon_deg = 0.0;
        p->params.ring_cyl.clockwise_lon  = 0;
        if (params) p->params.ring_cyl = params->ring_cyl;
        if (p->params.ring_cyl.center_radius == 0.0) {
            G_warning(_("p_proj_planet: RingCylindrical center_radius must not be 0"));
            G_free(p); return NULL;
        }
        break;

    case P_PROJ_LUNAR_AZIMUTHAL_EA:
        p->params.lunar_ea.equatorial_radius  = 1737.4;
        p->params.lunar_ea.max_libration_deg  = 8.0;
        if (params) p->params.lunar_ea = params->lunar_ea;
        if (p->params.lunar_ea.equatorial_radius <= 0.0) {
            G_warning(_("p_proj_planet: LunarAzimuthalEA radius must be > 0"));
            G_free(p); return NULL;
        }
        break;

    case P_PROJ_UPTURNED_TA:
        p->params.upturned_ta.a              = 1.0;
        p->params.upturned_ta.b              = 1.0;
        p->params.upturned_ta.center_lon_deg = 0.0;
        if (params) p->params.upturned_ta = params->upturned_ta;
        {
            double a = p->params.upturned_ta.a;
            double b = p->params.upturned_ta.b;
            if (a <= 0.0 || b <= 0.0 || b > a) {
                G_warning(_("p_proj_planet: UpturnedTA requires 0 < b <= a"));
                G_free(p); return NULL;
            }
            /* Ensure a >= b (major >= minor). */
            if (b > a) { double tmp=a; a=b; b=tmp; }
            p->ta_a = a; p->ta_b = b;
            p->ta_e = sqrt(1.0 - (b/a)*(b/a));
            double t0 = b / a;  /* = sqrt(1 - e^2) */
            p->ta_t  = t0 * t0;               /* 1 - e^2 */
            p->ta_t1 = p->ta_e * t0;           /* e*sqrt(1-e^2) */
            double k1 = 2.0 * a * exp(p->ta_t1 * atan(p->ta_t1));
            p->ta_k  = k1 * p->ta_t;
            p->ta_lambda0 = p->params.upturned_ta.center_lon_deg * DEG2RAD;
        }
        break;

    default:
        G_warning(_("p_proj_planet: unknown projection type %d"), (int)type);
        G_free(p); return NULL;
    }
    return (PProjPlanet *)p;
}

void p_proj_planet_free(PProjPlanet *p) { if (p) G_free(p); }

/* ================================================================== */
/* Forward / inverse dispatch                                           */
/* ================================================================== */

int p_proj_planet_fwd(const PProjPlanet *pp,
                       double coord1, double coord2,
                       double *x, double *y)
{
    const struct PProjPlanet *p = (const struct PProjPlanet *)pp;
    if (!p || !x || !y) return 0;
    switch (p->type) {
    case P_PROJ_RING_CYL:          return ring_cyl_fwd (p, coord1, coord2, x, y);
    case P_PROJ_LUNAR_AZIMUTHAL_EA: return lunar_ea_fwd (p, coord1, coord2, x, y);
    case P_PROJ_UPTURNED_TA:        return ueta_fwd    (p, coord1, coord2, x, y);
    default:                         return 0;
    }
}

int p_proj_planet_inv(const PProjPlanet *pp,
                       double x, double y,
                       double *coord1, double *coord2)
{
    const struct PProjPlanet *p = (const struct PProjPlanet *)pp;
    if (!p || !coord1 || !coord2) return 0;
    switch (p->type) {
    case P_PROJ_RING_CYL:          return ring_cyl_inv (p, x, y, coord1, coord2);
    case P_PROJ_LUNAR_AZIMUTHAL_EA: return lunar_ea_inv (p, x, y, coord1, coord2);
    case P_PROJ_UPTURNED_TA:        return ueta_inv    (p, x, y, coord1, coord2);
    default:                         return 0;
    }
}

/* ================================================================== */
/* Row-processing with OpenMP                                           */
/* ================================================================== */

void p_proj_planet_apply_row_fwd(const PProjPlanet *p,
                                  int nsamples,
                                  const double *coord1, const double *coord2,
                                  double *x_out, double *y_out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        if (!p_proj_planet_fwd(p, coord1[s], coord2[s],
                                &x_out[s], &y_out[s])) {
            x_out[s] = NAN;
            y_out[s] = NAN;
        }
    }
}

void p_proj_planet_apply_row_inv(const PProjPlanet *p,
                                  int nsamples,
                                  const double *x, const double *y,
                                  double *coord1_out, double *coord2_out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        if (!p_proj_planet_inv(p, x[s], y[s],
                                &coord1_out[s], &coord2_out[s])) {
            coord1_out[s] = NAN;
            coord2_out[s] = NAN;
        }
    }
}

/* ================================================================== */
/* Utility                                                              */
/* ================================================================== */

const char *p_proj_planet_name(const PProjPlanet *p)
{
    if (!p) return "NULL";
    switch (((const struct PProjPlanet *)p)->type) {
    case P_PROJ_RING_CYL:           return "RingCylindrical";
    case P_PROJ_LUNAR_AZIMUTHAL_EA: return "LunarAzimuthalEqualArea";
    case P_PROJ_UPTURNED_TA:        return "UpturnedEllipsoidTransverseAzimuthal";
    default:                         return "Unknown";
    }
}
