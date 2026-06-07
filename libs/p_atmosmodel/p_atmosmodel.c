/*!
 * \file p_atmosmodel.c
 *
 * \brief Planetary library - atmospheric scattering models (implementation).
 *
 * Algorithms ported from ISIS3 AtmosModel subclasses (USGS Astrogeology,
 * original authors Randy Kirk, Janet Barrett, K Teal Thompson; CC0-1.0).
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_atmosmodel.h"

#include <math.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_ATMOSMODEL_STANDALONE
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
#define EULGAM  0.5772156649   /* Euler-Mascheroni constant */
#define ATM_MAXEXP 69.0        /* exp clamp to avoid overflow */
#define ATM_FPMIN  1.0e-30     /* near-zero floor */

/* ================================================================== */
/* Special-function helpers (exposed via header for unit tests)         */
/* ================================================================== */

double p_atm_En(unsigned int n, double x)
{
    const int    MAXIT   = 100;
    const double EPSILON = 1.0e-7;
    int nm1 = (int)n - 1;
    double result, a, b, c, d, h, delta, fact, psi;

    if (x < 0.0 || (x == 0.0 && (n == 0 || n == 1))) {
        G_warning(_("p_atm_En: domain error n=%u x=%g"), n, x);
        return NAN;
    }
    if (n == 0)  return exp(-x) / x;
    if (x == 0.0) return 1.0 / nm1;

    if (x > 1.0) {
        /* Lentz continued-fraction */
        b = x + n;
        c = 1.0 / ATM_FPMIN;
        d = 1.0 / b;
        h = d;
        for (int i = 1; i <= MAXIT; i++) {
            a = -(double)i * (nm1 + i);
            b += 2.0;
            d  = 1.0 / (a * d + b);
            c  = b + a / c;
            delta = c * d;
            h *= delta;
            if (fabs(delta - 1.0) < EPSILON)
                return h * exp(-x);
        }
        G_warning(_("p_atm_En: CF did not converge for n=%u x=%g"), n, x);
        return h * exp(-x);
    }

    /* Series representation */
    result = (nm1 != 0) ? 1.0 / nm1 : -(log(x) + EULGAM);
    fact   = 1.0;
    for (int i = 1; i <= MAXIT; i++) {
        fact = -fact * x / i;
        if (i != nm1) {
            delta = -fact / (i - nm1);
        } else {
            psi = -EULGAM;
            for (int ii = 1; ii <= nm1; ii++) psi += 1.0 / ii;
            delta = fact * (-log(x) + psi);
        }
        result += delta;
        if (fabs(delta) < fabs(result) * EPSILON)
            return result;
    }
    G_warning(_("p_atm_En: series did not converge for n=%u x=%g"), n, x);
    return result;
}

/* ------------------------------------------------------------------ */

double p_atm_Ei(double x)
{
    const int    MAXIT   = 100;
    const double EPSILON = 6.0e-8;

    if (x <= 0.0) {
        G_warning(_("p_atm_Ei: domain error, requires x>0, got %g"), x);
        return NAN;
    }
    if (x < ATM_FPMIN)
        return log(x) + EULGAM;

    if (x <= -log(EPSILON)) {
        /* Power series */
        double sum = 0.0, fact = 1.0, term;
        for (int k = 1; k <= MAXIT; k++) {
            fact *= x / k;
            term  = fact / k;
            sum  += term;
            if (term < EPSILON * sum)
                return sum + log(x) + EULGAM;
        }
        return sum + log(x) + EULGAM;
    }

    /* Asymptotic series */
    double sum = 0.0, term = 1.0, prev;
    for (int k = 1; k <= MAXIT; k++) {
        prev  = term;
        term *= k / x;
        if (term < EPSILON)
            return exp(x) * (1.0 + sum) / x;
        if (term < prev)
            sum += term;
        else {
            sum -= prev;
            return exp(x) * (1.0 + sum) / x;
        }
    }
    return exp(x) * (1.0 + sum) / x;
}

/* ------------------------------------------------------------------ */

double p_atm_G11Prime(double tau)
{
    const double tol    = 1.0e-6;
    const double eulgam = 0.5772156;

    double sum = 0.0, fac = -tau, term = fac;
    int icnt = 1;
    while (fabs(term) > fabs(sum) * tol) {
        sum  += term;
        icnt++;
        fac  *= (-tau) / icnt;
        term  = fac / ((double)icnt * icnt);
    }
    double elog = log(fmax(ATM_FPMIN, tau)) + eulgam;
    double e1_2 = sum + M_PI * M_PI / 12.0 + 0.5 * elog * elog;
    return 2.0 * (p_atm_En(1, tau) + elog * p_atm_En(2, tau) - tau * e1_2);
}

/* ================================================================== */
/* Planetary curvature path-length correction                           */
/* ================================================================== */

/*
 * Given cosine of a zenith angle and atmospheric parameters, return the
 * effective path-length parameter munotp (or mup for emission).
 * Implements: hpsq1 = (1+hnorm)^2 - 1
 *             mup   = hnorm / (sqrt(hpsq1 + mu^2) - mu)
 * clamped to tau/69 to avoid log underflow.
 */
static inline double curved_path(double mu, double hnorm, double tau)
{
    double hpsq1 = (1.0 + hnorm) * (1.0 + hnorm) - 1.0;
    double maxval = fmax(ATM_FPMIN, hpsq1 + mu * mu);
    double mup    = hnorm / (sqrt(maxval) - mu);
    return fmax(mup, tau / ATM_MAXEXP);
}

/* Safe exponential transmittance exp(-tau/mu), clamped. */
static inline double safe_exp_trans(double tau, double mup)
{
    double xx = -tau / fmax(mup, ATM_FPMIN);
    if (xx < -ATM_MAXEXP) return 0.0;
    if (xx >  ATM_MAXEXP) return 1.0e30;
    return exp(xx);
}

/* ================================================================== */
/* Per-model cache structs                                              */
/* ================================================================== */

/* Isotropic1 / shared base: tau-dependent precomputed quantities. */
typedef struct {
    double tau_old, wha_old;       /* dirty-check values              */
    /* Exponential integrals */
    double e2, e3, e4, e5;
    /* Moments */
    double x0, y0, delta;
    double alpha0, alpha1;
    double beta0,  beta1;
    /* Conservative case */
    double alpha2, beta2, fixcon;
    int    conservative;
    /* Gamma weights and sbar */
    double gammax, gammay, sbar;
    double wha2;
} CacheIso1;

/* Isotropic2: adds Chandra G-function terms. */
typedef struct {
    CacheIso1 base;
    double e1, e1_2;
    double em, e, e5;           /* exp(±tau) and E_5 */
    double f1m, f2m, f3m, f4m; /* f-functions at mu=-1 */
    double f1, f2, f3, f4;     /* f-functions at mu=+1 */
    double g12, g13, g11p, g12p, g13p; /* G-functions */
    double g14, g14p;           /* conservative only    */
} CacheIso2;

/* Anisotropic1: tau-dependent for both m=0 and m=1 harmonics. */
typedef struct {
    double tau_old, wha_old;
    double e2, e3, e4, e5;
    double wha2, wham;
    /* m=0 */
    double x0_0, y0_0, delta_0;
    double alpha0_0, alpha1_0;
    double beta0_0, beta1_0;
    double fac, den;
    double q0, p0, q02p02;
    double q1, p1, q12p12;
    double sbar;
    /* m=1 */
    double x0_1, y0_1, delta_1;
} CacheAniso1;

/* Anisotropic2: extends Iso2 and Aniso1. */
typedef struct {
    double tau_old, wha_old;
    double e1, e1_2, e2, e3, e4, e5;
    double em, e;
    /* f-functions */
    double f1m, f2m, f3m, f4m;
    double f1, f2, f3, f4;
    /* G functions */
    double g12, g13, g14, g32, g33, g34;
    double g11p, g12p, g13p, g14p, g32p, g33p, g34p;
    /* m=0 moments */
    double wha2, wham;
    double x0_0, y0_0, delta_0;
    double alpha0_0, alpha1_0;
    double beta0_0, beta1_0;
    double fac, den;
    double q0, p0, q02p02;
    double q1, p1, q12p12;
    double sbar;
    /* m=1 moments */
    double x0_1, y0_1, delta_1;
} CacheAniso2;

/* ================================================================== */
/* Internal model struct                                                */
/* ================================================================== */

struct PAtmosModel {
    PAtmosModelType type;
    PAtmParams      params;
    union {
        CacheIso1   iso1;
        CacheIso2   iso2;
        CacheAniso1 aniso1;
        CacheAniso2 aniso2;
    } cache;
};

/* ================================================================== */
/* Cache update helpers                                                 */
/* ================================================================== */

/* Returns 1 if tau or wha changed relative to the stored old values. */
static int tau_or_wha_changed(double tau, double wha,
                               double *tau_old, double *wha_old)
{
    if (tau == *tau_old && wha == *wha_old) return 0;
    *tau_old = tau; *wha_old = wha;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Update Isotropic1 cache                                             */
/* ------------------------------------------------------------------ */
static void update_iso1(CacheIso1 *c, double tau, double wha)
{
    if (!tau_or_wha_changed(tau, wha, &c->tau_old, &c->wha_old)) return;

    c->wha2 = 0.5 * wha;
    c->e2   = p_atm_En(2, tau);
    c->e3   = p_atm_En(3, tau);
    c->e4   = p_atm_En(4, tau);

    c->x0    = c->wha2;
    c->y0    = c->wha2 * c->e2;
    c->delta = (1.0 - (c->x0 + c->y0) - (1.0 - wha) /
                (1.0 - (c->x0 - c->y0))) / (wha * (0.5 - c->e3));

    c->alpha0 = 1.0 + c->delta * (0.5 - c->e3);
    c->alpha1 = 0.5 + c->delta * (1.0/3.0 - c->e4);
    c->beta0  = c->e2 + c->delta * (0.5 - c->e3);
    c->beta1  = c->e3 + c->delta * (1.0/3.0 - c->e4);

    c->conservative = (wha == 1.0);
    if (c->conservative) {
        c->e5     = p_atm_En(5, tau);
        c->alpha2 = 1.0/3.0 + c->delta * (0.25 - c->e5);
        c->beta2  = c->e4   + c->delta * (0.25 - c->e5);
        c->fixcon = (c->beta0 * tau - c->alpha1 + c->beta1) /
                    ((c->alpha1 + c->beta1) * tau + 2.0 * (c->alpha2 + c->beta2));
    }

    c->gammax = c->wha2 * c->beta0;
    c->gammay = 1.0 - c->wha2 * c->alpha0;
    c->sbar   = 1.0 - ((2.0 - wha * c->alpha0) * c->alpha1 +
                        wha * c->beta0 * c->beta1);
}

/* ------------------------------------------------------------------ */
/* x/y functions of one path-length parameter (Iso1 first-order)       */
/* ------------------------------------------------------------------ */
static void iso1_xy(const CacheIso1 *c, double tau, double mup,
                     double *xm, double *ym)
{
    double em = safe_exp_trans(tau, mup);
    *xm = 1.0 + c->delta * mup * (1.0 - em);
    *ym = em + c->delta * mup * (1.0 - em);

    if (c->conservative) {
        double fix = c->fixcon * mup * (*xm + *ym);
        *xm += fix;
        *ym += fix;
    }
}

/* ------------------------------------------------------------------ */
/* Update Isotropic2 cache                                             */
/* ------------------------------------------------------------------ */
static void update_iso2(CacheIso2 *c, double tau, double wha)
{
    if (!tau_or_wha_changed(tau, wha, &c->base.tau_old, &c->base.wha_old))
        return;

    /* Start with Iso1 base quantities (sets base.e2/e3/e4/e5). */
    update_iso1(&c->base, tau, wha);
    double wha2 = c->base.wha2;
    /* Alias base exponential integrals for readability below. */
    double be2 = c->base.e2, be3 = c->base.e3, be4 = c->base.e4, be5 = c->base.e5;

    c->e1   = p_atm_En(1, tau);
    c->e1_2 = p_atm_En(1, 2.0 * tau);
    c->e5   = be5;   /* already computed by base */

    double xx = -tau;
    c->em = (xx < -ATM_MAXEXP) ? 0.0 : (xx > ATM_MAXEXP ? 1.0e30 : exp(xx));
    c->f1m = log(2.0) - c->em * c->e1 + c->e1_2;
    c->f2m = -(c->f1m + c->em * be2 - 1.0);
    c->f3m = -(c->f2m + c->em * be3 - 0.5);

    xx = tau;
    c->e = (xx < -ATM_MAXEXP) ? 0.0 : (xx > ATM_MAXEXP ? 1.0e30 : exp(xx));
    c->f1 = EULGAM + log(tau) + c->e * c->e1;
    c->f2 = c->f1 + c->e * be2 - 1.0;
    c->f3 = c->f2 + c->e * be3 - 0.5;

    c->g12  = (tau * c->e1 * be2 + c->f1m + c->f2m) * 0.5;
    c->g13  = (tau * c->e1 * be3 + c->f1m + c->f3m) * (1.0/3.0);
    c->g11p = p_atm_G11Prime(tau);
    c->g12p = (tau * (c->e1 - c->g11p) + c->em * (c->f1 + c->f2)) * 0.25;
    c->g13p = (tau * (0.5 * c->e1 - c->g12p) + c->em * (c->f1 + c->f3)) * 0.2;

    /* Override Iso1 moments with 2nd-order corrections. */
    c->base.x0    = wha2 * (1.0 + wha2 * c->g12);
    c->base.y0    = wha2 * (be2 + wha2 * c->g12p);
    c->base.delta = (1.0 - (c->base.x0 + c->base.y0) - (1.0 - wha) /
                     (1.0 - (c->base.x0 - c->base.y0))) / (wha * (0.5 - be3));

    c->base.alpha0 = 1.0 + wha2 * c->g12  + c->base.delta * (0.5      - be3);
    c->base.alpha1 = 0.5 + wha2 * c->g13  + c->base.delta * (1.0/3.0  - be4);
    c->base.beta0  = be2 + wha2 * c->g12p + c->base.delta * (0.5      - be3);
    c->base.beta1  = be3 + wha2 * c->g13p + c->base.delta * (1.0/3.0  - be4);

    if (c->base.conservative) {
        c->f4m = -(c->f3m + c->em * be4 - 1.0/3.0);
        double g14  = (tau * c->e1 * be4 + c->f1m + c->f4m) * 0.25;
        c->f4 = c->f3 + c->e * be4 - 1.0/3.0;
        c->g14p = (tau * (0.5 * c->e1 - c->g13p) + c->em * (c->f1 + c->f4)) * 0.2;
        c->base.alpha2 = 1.0/3.0 + wha2 * g14   + c->base.delta * (0.25 - be5);
        c->base.beta2  = be4     + wha2 * c->g14p + c->base.delta * (0.25 - be5);
        c->base.fixcon = (c->base.beta0 * tau - c->base.alpha1 + c->base.beta1) /
                         ((c->base.alpha1 + c->base.beta1) * tau +
                          2.0 * (c->base.alpha2 + c->base.beta2));
    }

    c->base.gammax = wha2 * c->base.beta0;
    c->base.gammay = 1.0 - wha2 * c->base.alpha0;
    c->base.sbar   = 1.0 - ((2.0 - wha * c->base.alpha0) * c->base.alpha1 +
                              wha * c->base.beta0 * c->base.beta1);
}

/* x/y for Iso2 second-order (includes f1 function). */
static void iso2_xy(const CacheIso2 *c, double tau, double mup,
                     double *xm, double *ym)
{
    const CacheIso1 *b = &c->base;
    double em = safe_exp_trans(tau, mup);

    double f1p, f1m_loc;
    if (fabs(mup - 1.0) < 1.0e-10) {
        f1p    = c->f1;
        f1m_loc = mup * (log(1.0 + 1.0/mup) - c->e1 * em +
                          p_atm_En(1, tau * (1.0 + 1.0/mup)));
    } else if (mup > 0.0) {
        f1p    = mup * (log(mup / (1.0 - mup)) + c->e1 / em +
                         p_atm_Ei(tau * (1.0/mup - 1.0)));
        f1m_loc = mup * (log(1.0 + 1.0/mup) - c->e1 * em +
                          p_atm_En(1, tau * (1.0 + 1.0/mup)));
    } else {
        /* fallback — very thick atmosphere or extreme geometry */
        f1p = 0.0; f1m_loc = 0.0;
    }

    *xm = 1.0 + b->wha2 * f1m_loc + b->delta * mup * (1.0 - em);
    *ym = em * (1.0 + b->wha2 * f1p) + b->delta * mup * (1.0 - em);

    if (b->conservative) {
        double fix = b->fixcon * mup * (*xm + *ym);
        *xm += fix;
        *ym += fix;
    }
}

/* ------------------------------------------------------------------ */
/* Update Anisotropic1 cache                                           */
/* ------------------------------------------------------------------ */
static void update_aniso1(CacheAniso1 *c, double tau, double wha, double bha)
{
    if (!tau_or_wha_changed(tau, wha, &c->tau_old, &c->wha_old)) return;

    c->wha2 = 0.5 * wha;
    c->wham = 1.0 - wha;
    c->e2   = p_atm_En(2, tau);
    c->e3   = p_atm_En(3, tau);
    c->e4   = p_atm_En(4, tau);
    c->e5   = p_atm_En(5, tau);

    /* m=0 moments */
    c->x0_0   = c->wha2 * (1.0 + (1.0/3.0) * bha * c->wham);
    c->y0_0   = c->wha2 * (c->e2 + bha * c->wham * c->e4);
    c->delta_0 = (1.0 - (c->x0_0 + c->y0_0) - (1.0 - wha * (1.0 + (1.0/3.0) * bha * c->wham)) /
                  (1.0 - (c->x0_0 - c->y0_0))) /
                 (wha * (0.5 - c->e3 + bha * c->wham * (0.25 - c->e5)));

    c->alpha0_0 = 1.0 + c->delta_0 * (0.5 - c->e3);
    c->alpha1_0 = 0.5 + c->delta_0 * (1.0/3.0 - c->e4);
    c->beta0_0  = c->e2 + c->delta_0 * (0.5 - c->e3);
    c->beta1_0  = c->e3 + c->delta_0 * (1.0/3.0 - c->e4);

    c->fac = 2.0 - wha * c->alpha0_0;
    c->den = c->fac * c->fac - (wha * c->beta0_0) * (wha * c->beta0_0);
    c->q0  = bha * wha * c->wham *
              (c->fac * c->alpha1_0 - wha * c->beta0_0 * c->beta1_0) / c->den;
    c->p0  = bha * wha * c->wham *
              (-c->fac * c->beta1_0 - wha * c->beta0_0 * c->alpha1_0) / c->den;
    c->q02p02 = c->q0 * c->q0 - c->p0 * c->p0;
    c->q1     = 2.0 * c->wham * c->fac / c->den;
    c->p1     = 2.0 * c->wham * wha * c->beta0_0 / c->den;
    c->q12p12 = c->q1 * c->q1 - c->p1 * c->p1;
    c->sbar   = 1.0 - 2.0 * (c->q1 * c->alpha1_0 + c->p1 * c->beta1_0);

    /* m=1 moments */
    c->x0_1   = 0.5 * c->wha2 * bha * (1.0 - 1.0/3.0);
    c->y0_1   = 0.5 * c->wha2 * bha * (c->e2 - c->e4);
    c->delta_1 = (1.0 - (c->x0_1 + c->y0_1) - (1.0 - (1.0/3.0) * wha * bha) /
                  (1.0 - (c->x0_1 - c->y0_1))) /
                 (c->wha2 * bha * ((0.5 - 0.25) - (c->e3 - c->e5)));
}

/* ------------------------------------------------------------------ */
/* Update Anisotropic2 cache                                           */
/* ------------------------------------------------------------------ */
static void update_aniso2(CacheAniso2 *c, double tau, double wha, double bha)
{
    if (!tau_or_wha_changed(tau, wha, &c->tau_old, &c->wha_old)) return;

    c->wha2 = 0.5 * wha;
    c->wham = 1.0 - wha;

    c->e1   = p_atm_En(1, tau);
    c->e1_2 = p_atm_En(1, 2.0 * tau);
    c->e2   = p_atm_En(2, tau);
    c->e3   = p_atm_En(3, tau);
    c->e4   = p_atm_En(4, tau);
    c->e5   = p_atm_En(5, tau);

    /* exp(±tau) */
    double xx = -tau;
    c->em = (xx < -ATM_MAXEXP) ? 0.0 : (xx > ATM_MAXEXP ? 1.0e30 : exp(xx));
    xx    =  tau;
    c->e  = (xx < -ATM_MAXEXP) ? 0.0 : (xx > ATM_MAXEXP ? 1.0e30 : exp(xx));

    /* f-functions at mu=-1 */
    c->f1m = log(2.0) - c->em * c->e1 + c->e1_2;
    c->f2m = -(c->f1m + c->em * c->e2 - 1.0);
    c->f3m = -(c->f2m + c->em * c->e3 - 0.5);
    c->f4m = -(c->f3m + c->em * c->e4 - 1.0/3.0);

    /* G functions at mu=-1 */
    c->g12  = (tau * c->e1 * c->e2 + c->f1m + c->f2m) * 0.5;
    c->g13  = (tau * c->e1 * c->e3 + c->f1m + c->f3m) * (1.0/3.0);
    c->g14  = (tau * c->e1 * c->e4 + c->f1m + c->f4m) * 0.25;
    c->g32  = (tau * c->e3 * c->e2 + c->f3m + c->f2m) * 0.25;
    c->g33  = (tau * c->e3 * c->e3 + c->f3m + c->f3m) * 0.2;
    c->g34  = (tau * c->e3 * c->e4 + c->f3m * c->f4m) * (1.0/6.0);

    /* f-functions and G' at mu=+1 */
    c->f1 = EULGAM + log(tau) + c->e * c->e1;
    c->f2 = c->f1 + c->e * c->e2 - 1.0;
    c->f3 = c->f2 + c->e * c->e3 - 0.5;
    c->f4 = c->f3 + c->e * c->e4 - 1.0/3.0;
    c->g11p = p_atm_G11Prime(tau);
    c->g12p = (tau * (c->e1 - c->g11p)  + c->em * (c->f1 + c->f2)) * 0.25;
    c->g13p = (tau * (0.5*c->e1 - c->g12p) + c->em * (c->f1 + c->f3)) * 0.2;
    c->g14p = (tau * ((1.0/3.0)*c->e1 - c->g13p) + c->em * (c->f1 + c->f4)) * (1.0/6.0);
    c->g32p = (tau * (c->e1 - c->g13p)  + c->em * (c->f3 + c->f2)) * (1.0/6.0);
    c->g33p = (tau * (0.5*c->e1 - c->g32p) + c->em * (c->f3 + c->f3)) * 0.142857;
    c->g34p = (tau * ((1.0/3.0)*c->e1 - c->g33p) + c->em * (c->f3 + c->f4)) * 0.125;

    /* m=0 zeroth moments */
    double bha2 = bha * bha;
    c->x0_0 = c->wha2 * (1.0 + (1.0/3.0)*bha*c->wham + c->wha2 *
              (c->g12 + bha*c->wham*(c->g14 + c->g32) + bha2*c->wham*c->wham*c->g34));
    c->y0_0 = c->wha2 * (c->e2 + bha*c->wham*c->e4 + c->wha2 *
              (c->g12p + bha*c->wham*(c->g14p + c->g32p) + bha2*c->wham*c->wham*c->g34p));

    c->delta_0 = (1.0 - (c->x0_0 + c->y0_0) - (1.0 - wha*(1.0 + (1.0/3.0)*bha*c->wham)) /
                  (1.0 - (c->x0_0 - c->y0_0))) /
                 (wha * (0.5 - c->e3 + bha * c->wham * (0.25 - c->e5)));

    c->alpha0_0 = 1.0 + c->wha2*(c->g12 + bha*c->wham*c->g32) + c->delta_0*(0.5 - c->e3);
    c->alpha1_0 = 0.5 + c->wha2*(c->g13 + bha*c->wham*c->g33) + c->delta_0*(1.0/3.0 - c->e4);
    c->beta0_0  = c->e2 + c->wha2*(c->g12p + bha*c->wham*c->g32p) + c->delta_0*(0.5 - c->e3);
    c->beta1_0  = c->e3 + c->wha2*(c->g13p + bha*c->wham*c->g33p) + c->delta_0*(1.0/3.0 - c->e4);

    c->fac = 2.0 - wha * c->alpha0_0;
    c->den = c->fac*c->fac - (wha*c->beta0_0)*(wha*c->beta0_0);
    c->q0  = bha*wha*c->wham*(c->fac*c->alpha1_0 - wha*c->beta0_0*c->beta1_0) / c->den;
    c->p0  = bha*wha*c->wham*(-c->fac*c->beta1_0 - wha*c->beta0_0*c->alpha1_0) / c->den;
    c->q02p02 = c->q0*c->q0 - c->p0*c->p0;
    c->q1     = 2.0*c->wham*c->fac / c->den;
    c->p1     = 2.0*c->wham*wha*c->beta0_0 / c->den;
    c->q12p12 = c->q1*c->q1 - c->p1*c->p1;
    c->sbar   = 1.0 - 2.0*(c->q1*c->alpha1_0 + c->p1*c->beta1_0);

    /* m=1 zeroth moments */
    c->x0_1 = 0.5*c->wha2*bha*(1.0 - 1.0/3.0 + 0.5*c->wha2*bha*(c->g12 - (c->g14+c->g32) + c->g34));
    c->y0_1 = 0.5*c->wha2*bha*(c->e2 - c->e4 + 0.5*c->wha2*bha*(c->g12p - (c->g14p+c->g32p) + c->g34p));
    c->delta_1 = (1.0 - (c->x0_1 + c->y0_1) - (1.0 - (1.0/3.0)*wha*bha) /
                  (1.0 - (c->x0_1 - c->y0_1))) /
                 (c->wha2*bha*((0.5 - 0.25) - (c->e3 - c->e5)));
}

/* Anisotropic x/y functions (shared m=0 and m=1 structure for Aniso1). */
static void aniso1_xy(double delta, double tau, double mup,
                       double *xm, double *ym)
{
    double em = safe_exp_trans(tau, mup);
    *xm = 1.0 + delta * mup * (1.0 - em);
    *ym = em  + delta * mup * (1.0 - em);
}

/* Anisotropic2 x/y for m=0 (includes f1 and f3 functions). */
static void aniso2_xy0(const CacheAniso2 *c, double tau, double mup,
                        double *xm, double *ym)
{
    double em = safe_exp_trans(tau, mup);

    double f1p, f1m_loc, f3p, f3m_loc;
    if (fabs(mup - 1.0) < 1.0e-10) {
        f1p     = c->f1;
        f1m_loc = mup*(log(1.0+1.0/mup) - c->e1*em + p_atm_En(1, tau*(1.0+1.0/mup)));
    } else if (mup > 0.0) {
        f1p     = mup*(log(mup/(1.0-mup)) + c->e1/em + p_atm_Ei(tau*(1.0/mup-1.0)));
        f1m_loc = mup*(log(1.0+1.0/mup) - c->e1*em + p_atm_En(1, tau*(1.0+1.0/mup)));
    } else { f1p = f1m_loc = 0.0; }

    double f2p     = mup*(f1p     + c->e2/em - 1.0);
    double f2m_loc = -mup*(f1m_loc + c->e2*em - 1.0);
    f3p     = mup*(f2p     + c->e3/em - 0.5);
    f3m_loc = -mup*(f2m_loc + c->e3*em - 0.5);

    *xm = 1.0 + c->wha2*(f1m_loc + c->f4m*c->wham*f3m_loc) + c->delta_0*mup*(1.0 - em);
    *ym = em*(1.0 + c->wha2*(f1p + c->f4m*c->wham*f3p)) + c->delta_0*mup*(1.0 - em);
}

/* Anisotropic2 x/y for m=1. */
static void aniso2_xy1(const CacheAniso2 *c, double tau, double mup,
                        double *xm, double *ym)
{
    double em = safe_exp_trans(tau, mup);

    double f1p, f1m_loc;
    if (fabs(mup - 1.0) < 1.0e-10) {
        f1p     = c->f1;
        f1m_loc = mup*(log(1.0+1.0/mup) - c->e1*em + p_atm_En(1, tau*(1.0+1.0/mup)));
    } else if (mup > 0.0) {
        f1p     = mup*(log(mup/(1.0-mup)) + c->e1/em + p_atm_Ei(tau*(1.0/mup-1.0)));
        f1m_loc = mup*(log(1.0+1.0/mup) - c->e1*em + p_atm_En(1, tau*(1.0+1.0/mup)));
    } else { f1p = f1m_loc = 0.0; }

    double f3p, f3m_loc;
    {
        double f2p     = mup*(f1p + c->e2/em - 1.0);
        double f2m_loc2= -mup*(f1m_loc + c->e2*em - 1.0);
        f3p     = mup*(f2p + c->e3/em - 0.5);
        f3m_loc = -mup*(f2m_loc2 + c->e3*em - 0.5);
    }

    *xm = 1.0 + 0.5*c->wha2*c->f4m*(f1m_loc - f3m_loc) + c->delta_1*mup*(1.0 - em);
    *ym = em*(1.0 + 0.5*c->wha2*c->f4m*(f1p - f3p)) + c->delta_1*mup*(1.0 - em);
}

/* ================================================================== */
/* Algorithm runners                                                    */
/* ================================================================== */

static void run_iso1(struct PAtmosModel *m,
                      double pha_unused, double inc, double ema,
                      PAtmResult *r)
{
    (void)pha_unused;
    double tau   = m->params.tau;
    double wha   = m->params.wha;
    double hnorm = m->params.hnorm;
    CacheIso1 *c = &m->cache.iso1;

    update_iso1(c, tau, wha);

    double munot = cos(inc * DEG2RAD);
    double mu    = cos(ema * DEG2RAD);
    double munotp = curved_path(munot, hnorm, tau);
    double mup    = curved_path(mu,    hnorm, tau);

    double xmunot, ymunot, xmu, ymu;
    iso1_xy(c, tau, munotp, &xmunot, &ymunot);
    iso1_xy(c, tau, mup,    &xmu,    &ymu);

    double gmunot = c->gammax * xmunot + c->gammay * ymunot;
    double gmu    = c->gammax * xmu    + c->gammay * ymu;

    r->pstd   = 0.25 * wha * munotp / (munotp + mup) * (xmunot*xmu - ymunot*ymu);
    r->trans  = gmunot * gmu;
    r->trans0 = safe_exp_trans(tau, munotp) * safe_exp_trans(tau, mup);
    r->sbar   = c->sbar;

    double emunot = safe_exp_trans(tau, munotp);
    r->transs = (emunot + 0.5*(c->gammax*xmunot + c->gammay*ymunot - emunot)) *
                safe_exp_trans(tau, mup);
}

/* ------------------------------------------------------------------ */
static void run_iso2(struct PAtmosModel *m,
                      double pha_unused, double inc, double ema,
                      PAtmResult *r)
{
    (void)pha_unused;
    double tau   = m->params.tau;
    double wha   = m->params.wha;
    double hnorm = m->params.hnorm;
    CacheIso2 *c = &m->cache.iso2;

    update_iso2(c, tau, wha);

    double munot  = cos(inc * DEG2RAD);
    double mu     = cos(ema * DEG2RAD);
    double munotp = curved_path(munot, hnorm, tau);
    double mup    = curved_path(mu,    hnorm, tau);

    double xmunot, ymunot, xmu, ymu;
    iso2_xy(c, tau, munotp, &xmunot, &ymunot);
    iso2_xy(c, tau, mup,    &xmu,    &ymu);

    const CacheIso1 *b = &c->base;
    double gmunot = b->gammax * xmunot + b->gammay * ymunot;
    double gmu    = b->gammax * xmu    + b->gammay * ymu;

    r->pstd   = 0.25 * wha * munotp / (munotp + mup) * (xmunot*xmu - ymunot*ymu);
    r->trans  = gmunot * gmu;
    r->trans0 = safe_exp_trans(tau, munotp) * safe_exp_trans(tau, mup);
    r->sbar   = b->sbar;

    double emunot = safe_exp_trans(tau, munotp);
    r->transs = (emunot + 0.5*(b->gammax*xmunot + b->gammay*ymunot - emunot)) *
                safe_exp_trans(tau, mup);
}

/* ------------------------------------------------------------------ */
static void run_aniso1(struct PAtmosModel *m,
                        double pha, double inc, double ema,
                        PAtmResult *r)
{
    double tau   = m->params.tau;
    double wha   = m->params.wha;
    double hnorm = m->params.hnorm;
    double bha   = m->params.bha;
    if (bha == 0.0) bha = 1.0e-6;
    CacheAniso1 *c = &m->cache.aniso1;

    update_aniso1(c, tau, wha, bha);

    double munot = (inc == 90.0) ? 0.0 : cos(inc * DEG2RAD);
    double mu    = (ema == 90.0) ? 0.0 : cos(ema * DEG2RAD);
    double munotp = curved_path(munot, hnorm, tau);
    double mup    = curved_path(mu,    hnorm, tau);

    /* x/y for m=0 */
    double xmunot_0, ymunot_0, xmu_0, ymu_0;
    aniso1_xy(c->delta_0, tau, munotp, &xmunot_0, &ymunot_0);
    aniso1_xy(c->delta_0, tau, mup,    &xmu_0,    &ymu_0);

    /* x/y for m=1 */
    double xmunot_1, ymunot_1, xmu_1, ymu_1;
    aniso1_xy(c->delta_1, tau, munotp, &xmunot_1, &ymunot_1);
    aniso1_xy(c->delta_1, tau, mup,    &xmu_1,    &ymu_1);

    double gmunot = c->p1 * xmunot_0 + c->q1 * ymunot_0;
    double gmu    = c->p1 * xmu_0    + c->q1 * ymu_0;

    double sum  = munot + mu;
    double prod = munot * mu;
    double cxx  = 1.0 - c->q0*sum  + (c->q02p02 - bha*c->q12p12)*prod;
    double cyy  = 1.0 + c->q0*sum  + (c->q02p02 - bha*c->q12p12)*prod;

    double cosazss = (pha == 90.0) ? (-munot*mu) : (cos(pha * DEG2RAD) - munot*mu);

    double xystuff = cxx*xmunot_0*xmu_0 - cyy*ymunot_0*ymu_0
                   - c->p0*sum*(xmu_0*ymunot_0 + ymu_0*xmunot_0)
                   + cosazss*bha*(xmu_1*xmunot_1 - ymu_1*ymunot_1);

    r->pstd   = 0.25 * wha * munotp / (munotp + mup) * xystuff;
    r->trans  = gmunot * gmu;
    r->trans0 = safe_exp_trans(tau, munotp) * safe_exp_trans(tau, mup);
    r->sbar   = c->sbar;
    r->transs = r->trans0;  /* analytic expression not derived for P1 */
}

/* ------------------------------------------------------------------ */
static void run_aniso2(struct PAtmosModel *m,
                        double pha, double inc, double ema,
                        PAtmResult *r)
{
    double tau   = m->params.tau;
    double wha   = m->params.wha;
    double hnorm = m->params.hnorm;
    double bha   = m->params.bha;
    if (bha == 0.0) bha = 1.0e-6;
    CacheAniso2 *c = &m->cache.aniso2;

    update_aniso2(c, tau, wha, bha);

    double munot = (inc == 90.0) ? 0.0 : cos(inc * DEG2RAD);
    double mu    = (ema == 90.0) ? 0.0 : cos(ema * DEG2RAD);
    double munotp = curved_path(munot, hnorm, tau);
    double mup    = curved_path(mu,    hnorm, tau);

    double xmunot_0, ymunot_0, xmu_0, ymu_0;
    aniso2_xy0(c, tau, munotp, &xmunot_0, &ymunot_0);
    aniso2_xy0(c, tau, mup,    &xmu_0,    &ymu_0);

    double xmunot_1, ymunot_1, xmu_1, ymu_1;
    aniso2_xy1(c, tau, munotp, &xmunot_1, &ymunot_1);
    aniso2_xy1(c, tau, mup,    &xmu_1,    &ymu_1);

    double gmunot = c->p1 * xmunot_0 + c->q1 * ymunot_0;
    double gmu    = c->p1 * xmu_0    + c->q1 * ymu_0;

    double sum  = munot + mu;
    double prod = munot * mu;
    double cxx  = 1.0 - c->q0*sum  + (c->q02p02 - bha*c->q12p12)*prod;
    double cyy  = 1.0 + c->q0*sum  + (c->q02p02 - bha*c->q12p12)*prod;
    double cosazss = (pha == 90.0) ? (-munot*mu) : (cos(pha * DEG2RAD) - munot*mu);

    double xystuff = cxx*xmunot_0*xmu_0 - cyy*ymunot_0*ymu_0
                   - c->p0*sum*(xmu_0*ymunot_0 + ymu_0*xmunot_0)
                   + cosazss*bha*(xmu_1*xmunot_1 - ymu_1*ymunot_1);

    r->pstd   = 0.25 * wha * munotp / (munotp + mup) * xystuff;
    r->trans  = gmunot * gmu;
    r->trans0 = safe_exp_trans(tau, munotp) * safe_exp_trans(tau, mup);
    r->sbar   = c->sbar;
    r->transs = r->trans0;
}

/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

PAtmosModel *p_atmosmodel_create(PAtmosModelType type, const PAtmParams *params)
{
    struct PAtmosModel *m =
        (struct PAtmosModel *)G_malloc(sizeof(struct PAtmosModel));
    memset(m, 0, sizeof(*m));
    m->type = type;

    /* Install defaults. */
    m->params.tau   = 0.28;
    m->params.wha   = 0.95;
    m->params.hnorm = 0.05;
    m->params.tauref= 0.0;
    m->params.bha   = 0.85;

    if (params) m->params = *params;

    /* Validate */
    if (m->params.tau < 0.0) {
        G_warning(_("p_atmosmodel: tau must be >=0, got %g"), m->params.tau);
        G_free(m); return NULL;
    }
    if (m->params.wha <= 0.0 || m->params.wha > 1.0) {
        G_warning(_("p_atmosmodel: wha must be in (0,1], got %g"), m->params.wha);
        G_free(m); return NULL;
    }
    if ((type == P_ATMOSMODEL_ANISOTROPIC1 || type == P_ATMOSMODEL_ANISOTROPIC2)
        && m->params.wha == 1.0) {
        G_warning(_("p_atmosmodel: Anisotropic conservative case (wha=1) not implemented"));
        G_free(m); return NULL;
    }

    /* Initialise dirty-check sentinels so first call triggers cache build. */
    m->cache.iso1.tau_old = -1.0;
    m->cache.iso1.wha_old = -1.0;

    return (PAtmosModel *)m;
}

/* ------------------------------------------------------------------ */

int p_atmosmodel_eval(PAtmosModel *pm,
                       double phase_deg, double inc_deg, double ema_deg,
                       PAtmResult *result)
{
    if (!pm || !result) return -1;
    struct PAtmosModel *m = (struct PAtmosModel *)pm;

    /* Vacuum: no atmosphere. */
    if (m->params.tau == 0.0) {
        result->pstd = result->sbar = 0.0;
        result->trans = result->trans0 = result->transs = 1.0;
        return 0;
    }

    switch (m->type) {
    case P_ATMOSMODEL_ISOTROPIC1:
        run_iso1(m, phase_deg, inc_deg, ema_deg, result); break;
    case P_ATMOSMODEL_ISOTROPIC2:
        run_iso2(m, phase_deg, inc_deg, ema_deg, result); break;
    case P_ATMOSMODEL_ANISOTROPIC1:
        run_aniso1(m, phase_deg, inc_deg, ema_deg, result); break;
    case P_ATMOSMODEL_ANISOTROPIC2:
        run_aniso2(m, phase_deg, inc_deg, ema_deg, result); break;
    default:
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

double p_atmosmodel_apply(double pstd, double trans, double trans0,
                           double sbar, double rho, double Ah, double Ab,
                           double Psurf, double munot)
{
    double denom = 1.0 - rho * Ab * sbar;
    if (fabs(denom) < 1.0e-12) denom = 1.0e-12;
    return pstd
         + trans  * rho * Ah * munot / denom
         + trans0 * rho * (Psurf - Ah * munot);
}

/* ------------------------------------------------------------------ */

void p_atmosmodel_apply_row(PAtmosModel *m,
                             int nsamples,
                             const double *input,
                             const double *psurf,
                             const double *phase,
                             const double *incidence,
                             const double *emission,
                             double rho,
                             double Ah,
                             double Ab,
                             double *output)
{
    int s;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        double dn = input[s];
        if (dn != dn) { output[s] = dn; continue; } /* NaN passthrough */

#ifdef _OPENMP
        /* Each thread gets its own model copy to avoid cache race. */
        struct PAtmosModel local = *(const struct PAtmosModel *)m;
        local.cache.iso1.tau_old = -1.0;
        local.cache.iso1.wha_old = -1.0;
        PAtmResult r;
        p_atmosmodel_eval((PAtmosModel *)&local,
                           phase[s], incidence[s], emission[s], &r);
#else
        PAtmResult r;
        p_atmosmodel_eval(m, phase[s], incidence[s], emission[s], &r);
#endif
        double munot = cos(incidence[s] * DEG2RAD);
        output[s] = p_atmosmodel_apply(r.pstd, r.trans, r.trans0,
                                        r.sbar, rho, Ah, Ab,
                                        psurf[s], munot);
    }
}

/* ------------------------------------------------------------------ */

const char *p_atmosmodel_name(const PAtmosModel *pm)
{
    if (!pm) return "NULL";
    switch (((const struct PAtmosModel *)pm)->type) {
    case P_ATMOSMODEL_ISOTROPIC1:   return "Isotropic1";
    case P_ATMOSMODEL_ISOTROPIC2:   return "Isotropic2";
    case P_ATMOSMODEL_ANISOTROPIC1: return "Anisotropic1";
    case P_ATMOSMODEL_ANISOTROPIC2: return "Anisotropic2";
    default:                         return "Unknown";
    }
}

/* ------------------------------------------------------------------ */

void p_atmosmodel_free(PAtmosModel *m)
{
    if (m) G_free(m);
}
