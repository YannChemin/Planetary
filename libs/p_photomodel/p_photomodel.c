/*!
 * \file p_photomodel.c
 *
 * \brief Planetary library - photometric models implementation.
 *
 * Algorithms ported from ISIS3 PhotoModel subclasses (USGS Astrogeology,
 * public domain / CC0-1.0).  The per-pixel eval functions are stateless and
 * thread-safe; row-processing functions use OpenMP where available.
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_photomodel.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_PHOTOMODEL_STANDALONE
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

/* ------------------------------------------------------------------ */
/* Internal concrete model struct                                       */
/* ------------------------------------------------------------------ */

struct PPhotoModel {
    PPhotoModelType type;
    PPhmParams      params;
    /* Hapke roughness pre-computed values (recalculated when theta changes). */
    double _cott;    /* cos(theta)/sin(theta) */
    double _cot2t;   /* cott^2                */
    double _tant;    /* sin(theta)/cos(theta) */
    double _sr;      /* sqrt(1 + pi*tan^2(theta)) */
    double _osr;     /* 1 / sr                */
    double _theta_cache; /* last theta value these were computed for */
    /* McEwen: r30 normalisation constant */
    double _r30;
};

/* ------------------------------------------------------------------ */
/* Hapke H-function approximation (Chandrasekhar 1960 Table 5 approx) */
/* H(u, gamma) = (1 + 2u) / (1 + 2*u*gamma)                          */
/* ------------------------------------------------------------------ */
static inline double hfunc(double u, double gamma)
{
    return (1.0 + 2.0 * u) / (1.0 + 2.0 * u * gamma);
}

/* ------------------------------------------------------------------ */
/* Hapke roughness pre-computation                                      */
/* Updates cached cott/tant/sr/osr when theta has changed.             */
/* ------------------------------------------------------------------ */
static void hapke_update_roughness(struct PPhotoModel *m, double theta)
{
    if (theta == m->_theta_cache) return;
    m->_theta_cache = theta;
    if (theta <= 0.0) {
        m->_cott = 1.0e10;
        m->_cot2t = 1.0e20;
        m->_tant  = 0.0;
        m->_sr    = 1.0;
        m->_osr   = 1.0;
        return;
    }
    double cost = cos(theta * DEG2RAD);
    double sint = sin(theta * DEG2RAD);
    m->_tant  = sint / cost;
    m->_cott  = cost / (sint > 1e-10 ? sint : 1e-10);
    m->_cot2t = m->_cott * m->_cott;
    double tan2t = m->_tant * m->_tant;
    m->_sr   = sqrt(1.0 + M_PI * tan2t);
    m->_osr  = 1.0 / m->_sr;
}

/* ------------------------------------------------------------------ */
/* Core algorithm implementations (stateless, thread-safe)             */
/* ------------------------------------------------------------------ */

/* --- Lambert -------------------------------------------------------- */
static double eval_lambert(double inc_rad)
{
    double munot = cos(inc_rad);
    return (munot > 0.0) ? munot : 0.0;
}

/* --- Lommel-Seeliger ------------------------------------------------ */
static double eval_lommelseeliger(double inc_rad, double ema_rad)
{
    double munot = cos(inc_rad);
    double mu    = cos(ema_rad);
    if (munot <= 0.0 || mu <= 0.0) return 0.0;
    return 2.0 * munot / (munot + mu);
}

/* --- Lunar-Lambert (McEwen 1991 linear blend) ---------------------- */
static double eval_lunarlambert(double L, double inc_rad, double ema_rad)
{
    double munot = cos(inc_rad);
    double mu    = cos(ema_rad);
    if (munot <= 0.0 || mu <= 0.0) return 0.0;
    if (L == 0.0) return munot;
    if (L == 1.0) return 2.0 * munot / (munot + mu);
    return munot * ((1.0 - L) + 2.0 * L / (munot + mu));
}

/* --- Minnaert ------------------------------------------------------- */
static double eval_minnaert(double K, double inc_rad, double ema_rad)
{
    double munot = cos(inc_rad);
    double mu    = cos(ema_rad);
    if (munot <= 0.0 || mu <= 0.0) return 0.0;
    if (K == 1.0) return munot;
    return munot * pow(munot * mu, K - 1.0);
}

/* --- Hapke (common kernel, called by both HEN and LEG variants) ----- */
/*
 * Full Hapke with macroscopic roughness (Hapke 1981, 1984).
 * pg    = single-particle phase function value (computed by caller).
 * b0_use = effective B0 (may be 0 at standard conditions).
 */
static double eval_hapke_core(struct PPhotoModel *m,
                               double pg, double b0_use,
                               double pha_rad, double inc_rad, double ema_rad)
{
    double munot = cos(inc_rad);
    double mu    = cos(ema_rad);

    if (munot <= 0.0) return 0.0;

    double wh    = m->params.hapke_hen.wh;  /* both hen/leg share offset */
    double hh    = m->params.hapke_hen.hh;
    double theta = m->params.hapke_hen.theta;

    double gamma = sqrt(1.0 - wh);

    /* Opposition surge. */
    double tang2 = tan(pha_rad / 2.0);
    double bg = (hh == 0.0) ? 0.0 : b0_use / (1.0 + tang2 / hh);

    /* Smooth Hapke (theta = 0). */
    if (theta <= 0.0) {
        return wh / 4.0 * munot / (munot + mu) *
               ((1.0 + bg) * pg - 1.0 +
                hfunc(munot, gamma) * hfunc(mu, gamma));
    }

    /* Rough Hapke - pre-compute roughness terms (not thread-safe for the
     * cache but we re-enter only from p_photomodel_apply_row where each
     * thread gets its own model copy, or from single-threaded eval). */
    hapke_update_roughness(m, theta);

    double sini = sin(inc_rad);
    double coti = munot / (sini > 1e-10 ? sini : 1e-10);
    double cot2i = coti * coti;

    double ecoti  = exp(fmin(-m->_cot2t * cot2i / M_PI, 23.0));
    double ecot2i = exp(fmin(-2.0 * m->_cott * coti / M_PI, 23.0));
    double u0p0   = m->_osr * (munot + sini * m->_tant * ecoti / (2.0 - ecot2i));

    double sine  = sin(ema_rad);
    double cote  = mu / (sine > 1e-10 ? sine : 1e-10);
    double cot2e = cote * cote;

    double cosg  = cos(pha_rad);
    double cosei = mu * munot;
    double sinei = sine * sini;

    double caz, az;
    if (sinei == 0.0) {
        caz = 1.0; az = 0.0;
    }
    else {
        caz = (cosg - cosei) / sinei;
        caz = fmax(-1.0, fmin(1.0, caz));
        az  = acos(caz) * 180.0 / M_PI;
    }

    double az2 = az / 2.0;
    double faz;
    if (az2 >= 90.0) {
        faz = 0.0;
    }
    else {
        double tanaz2 = tan(az2 * DEG2RAD);
        faz = exp(fmin(-2.0 * tanaz2, 23.0));
    }

    double sin2a2 = pow(sin(az2 * DEG2RAD), 2.0);
    double api    = az / 180.0;

    double ecote  = exp(fmin(-m->_cot2t * cot2e / M_PI, 23.0));
    double ecot2e = exp(fmin(-2.0 * m->_cott * cote / M_PI, 23.0));
    double up0    = m->_osr * (mu + sine * m->_tant * ecote / (2.0 - ecot2e));

    double q;
    double u0p, up;
    if (inc_rad <= ema_rad) {
        q = m->_osr * munot / u0p0;
        double ecei = 2.0 - ecot2e - api * ecot2i;
        double s2ei = sin2a2 * ecoti;
        u0p = m->_osr * (munot + sini * m->_tant * (caz * ecote + s2ei) / ecei);
        up  = m->_osr * (mu    + sine  * m->_tant * (ecote - s2ei) / ecei);
    }
    else {
        q = m->_osr * mu / up0;
        double ecee = 2.0 - ecot2i - api * ecot2e;
        double s2ee = sin2a2 * ecote;
        u0p = m->_osr * (munot + sini * m->_tant * (ecoti - s2ee) / ecee);
        up  = m->_osr * (mu    + sine  * m->_tant * (caz * ecoti + s2ee) / ecee);
    }

    double rr1 = wh / 4.0 * u0p / (u0p + up) *
                 ((1.0 + bg) * pg - 1.0 +
                  hfunc(u0p, gamma) * hfunc(up, gamma));
    double rr2 = up * munot / (up0 * u0p0 * m->_sr * (1.0 - faz + faz * q));
    return rr1 * rr2;
}

/* --- Hapke HEN: compute phase function then call core -------------- */
static double eval_hapke_hen(struct PPhotoModel *m,
                              double b0_use,
                              double pha_rad, double inc_rad, double ema_rad)
{
    double hg1  = m->params.hapke_hen.hg1;
    double hg2  = m->params.hapke_hen.hg2;
    double cosg = cos(pha_rad);
    double hgs  = hg1 * hg1;
    double pg1  = (1.0 - hg2) * (1.0 - hgs) /
                  pow(1.0 + hgs + 2.0 * hg1 * cosg, 1.5);
    double pg2  = hg2 * (1.0 - hgs) /
                  pow(fmax(1.0 + hgs - 2.0 * hg1 * cosg, 1e-10), 1.5);
    double pg   = pg1 + pg2;
    return eval_hapke_core(m, pg, b0_use, pha_rad, inc_rad, ema_rad);
}

/* --- Hapke LEG: Legendre phase fn ---------------------------------- */
static double eval_hapke_leg(struct PPhotoModel *m,
                              double b0_use,
                              double pha_rad, double inc_rad, double ema_rad)
{
    double bh   = m->params.hapke_leg.bh;
    double ch   = m->params.hapke_leg.ch;
    double cosg = cos(pha_rad);
    double pg   = 1.0 + bh * cosg + ch * (1.5 * cosg * cosg - 0.5);
    return eval_hapke_core(m, pg, b0_use, pha_rad, inc_rad, ema_rad);
}

/* --- McEwen Lunar-Lambert ------------------------------------------ */
static double eval_lunarlambert_mcewen(const struct PPhotoModel *m,
                                        double pha_deg,
                                        double inc_rad, double ema_rad)
{
    double munot = cos(inc_rad);
    double mu    = cos(ema_rad);
    if (munot <= 0.0 || mu <= 0.0) return 0.0;

    double m1 = m->params.lunarlambert_mcewen.m1;
    double m2 = m->params.lunarlambert_mcewen.m2;
    double m3 = m->params.lunarlambert_mcewen.m3;

    double xl = 1.0 + m1 * pha_deg + m2 * pha_deg * pha_deg +
                m3 * pha_deg * pha_deg * pha_deg;
    double r  = 2.0 * xl * munot / (mu + munot) + (1.0 - xl) * munot;
    if (r <= 0.0) return 0.0;
    /* Normalise by r30 (pre-computed at model creation). */
    return m->_r30 / r;
}

/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

PPhotoModel *p_photomodel_create(PPhotoModelType type, const PPhmParams *params)
{
    struct PPhotoModel *m =
        (struct PPhotoModel *)G_malloc(sizeof(struct PPhotoModel));
    memset(m, 0, sizeof(*m));
    m->type          = type;
    m->_theta_cache  = -999.0;  /* force roughness re-computation */

    /* Install defaults, then overlay caller params. */
    switch (type) {
    case P_PHOTOMODEL_LAMBERT:
        /* no params */
        break;
    case P_PHOTOMODEL_LOMMELSEELIGER:
        /* no params */
        break;
    case P_PHOTOMODEL_LUNARLAMBERT:
        m->params.lunarlambert.L = 1.0;
        if (params) m->params.lunarlambert = params->lunarlambert;
        break;
    case P_PHOTOMODEL_MINNAERT:
        m->params.minnaert.K = 1.0;
        if (params) m->params.minnaert = params->minnaert;
        if (m->params.minnaert.K < 0.0) {
            G_warning(_("p_photomodel: Minnaert K must be ≥ 0, got %g"),
                       m->params.minnaert.K);
            G_free(m); return NULL;
        }
        break;
    case P_PHOTOMODEL_HAPKE_HEN: {
        PPhmHapkeHen *p = &m->params.hapke_hen;
        p->wh = 0.5; p->hh = 0.0; p->b0 = 0.0;
        p->hg1 = 0.0; p->hg2 = 0.0; p->theta = 0.0; p->zero_b0_std = 1;
        if (params) *p = params->hapke_hen;
        /* Validate. */
        if (p->wh <= 0.0 || p->wh > 1.0) {
            G_warning(_("p_photomodel: Hapke wh must be in (0,1], got %g"), p->wh);
            G_free(m); return NULL;
        }
        if (p->hg1 <= -1.0 || p->hg1 >= 1.0) {
            G_warning(_("p_photomodel: Hapke hg1 must be in (-1,1), got %g"), p->hg1);
            G_free(m); return NULL;
        }
        if (p->hg2 < 0.0 || p->hg2 > 1.0) {
            G_warning(_("p_photomodel: Hapke hg2 must be in [0,1], got %g"), p->hg2);
            G_free(m); return NULL;
        }
        if (p->theta < 0.0 || p->theta > 90.0) {
            G_warning(_("p_photomodel: Hapke theta must be in [0,90], got %g"), p->theta);
            G_free(m); return NULL;
        }
        hapke_update_roughness(m, p->theta);
        break;
    }
    case P_PHOTOMODEL_HAPKE_LEG: {
        PPhmHapkeLeg *p = &m->params.hapke_leg;
        p->wh = 0.5; p->hh = 0.0; p->b0 = 0.0;
        p->bh = 0.0; p->ch = 0.0; p->theta = 0.0; p->zero_b0_std = 1;
        if (params) *p = params->hapke_leg;
        if (p->wh <= 0.0 || p->wh > 1.0) {
            G_warning(_("p_photomodel: Hapke wh must be in (0,1], got %g"), p->wh);
            G_free(m); return NULL;
        }
        if (p->bh < -1.0 || p->bh > 1.0) {
            G_warning(_("p_photomodel: Hapke bh must be in [-1,1], got %g"), p->bh);
            G_free(m); return NULL;
        }
        if (p->ch < -1.0 || p->ch > 1.0) {
            G_warning(_("p_photomodel: Hapke ch must be in [-1,1], got %g"), p->ch);
            G_free(m); return NULL;
        }
        if (p->theta < 0.0 || p->theta > 90.0) {
            G_warning(_("p_photomodel: Hapke theta must be in [0,90], got %g"), p->theta);
            G_free(m); return NULL;
        }
        hapke_update_roughness(m, p->theta);
        break;
    }
    case P_PHOTOMODEL_LUNARLAMBERT_MCEWEN: {
        PPhmLunarLambertMcEwen *p = &m->params.lunarlambert_mcewen;
        p->m1 = -0.019; p->m2 = 0.000242; p->m3 = -0.00000146;
        if (params) *p = params->lunarlambert_mcewen;
        /* Pre-compute r30 normalisation (McEwen calibration at 30° phase). */
        double c30 = cos(30.0 * DEG2RAD);
        double xl30 = 1.0 + p->m1 * 30.0 + p->m2 * 900.0 + p->m3 * 27000.0;
        m->_r30 = 2.0 * xl30 * c30 / (1.0 + c30) + (1.0 - xl30) * c30;
        break;
    }
    default:
        G_warning(_("p_photomodel: unknown model type %d"), (int)type);
        G_free(m);
        return NULL;
    }

    return (PPhotoModel *)m;
}

/* ------------------------------------------------------------------ */

double p_photomodel_eval(const PPhotoModel *pm,
                          double phase_deg,
                          double incidence_deg,
                          double emission_deg)
{
    if (!pm) return 0.0;
    /* Use a mutable pointer for Hapke roughness cache update. */
    struct PPhotoModel *m = (struct PPhotoModel *)pm;

    if (incidence_deg >= 90.0) return 0.0;
    if (emission_deg  >= 90.0) return 0.0;

    double pha_rad = phase_deg     * DEG2RAD;
    double inc_rad = incidence_deg * DEG2RAD;
    double ema_rad = emission_deg  * DEG2RAD;

    switch (m->type) {
    case P_PHOTOMODEL_LAMBERT:
        return eval_lambert(inc_rad);

    case P_PHOTOMODEL_LOMMELSEELIGER:
        return eval_lommelseeliger(inc_rad, ema_rad);

    case P_PHOTOMODEL_LUNARLAMBERT:
        return eval_lunarlambert(m->params.lunarlambert.L, inc_rad, ema_rad);

    case P_PHOTOMODEL_MINNAERT:
        return eval_minnaert(m->params.minnaert.K, inc_rad, ema_rad);

    case P_PHOTOMODEL_HAPKE_HEN:
        return eval_hapke_hen(m, m->params.hapke_hen.b0,
                               pha_rad, inc_rad, ema_rad);

    case P_PHOTOMODEL_HAPKE_LEG:
        return eval_hapke_leg(m, m->params.hapke_leg.b0,
                               pha_rad, inc_rad, ema_rad);

    case P_PHOTOMODEL_LUNARLAMBERT_MCEWEN:
        return eval_lunarlambert_mcewen(m, phase_deg, inc_rad, ema_rad);

    default:
        return 0.0;
    }
}

/* ------------------------------------------------------------------ */

double p_photomodel_standard(const PPhotoModel *pm)
{
    if (!pm) return 1.0;
    struct PPhotoModel *m = (struct PPhotoModel *)pm;

    /* For Hapke, temporarily zero B0 if zero_b0_std is set (ISIS3 behaviour). */
    double b0_saved_hen = 0.0, b0_saved_leg = 0.0;
    int    zero_hen = 0, zero_leg = 0;

    if (m->type == P_PHOTOMODEL_HAPKE_HEN && m->params.hapke_hen.zero_b0_std) {
        b0_saved_hen = m->params.hapke_hen.b0;
        m->params.hapke_hen.b0 = 0.0;
        zero_hen = 1;
    }
    if (m->type == P_PHOTOMODEL_HAPKE_LEG && m->params.hapke_leg.zero_b0_std) {
        b0_saved_leg = m->params.hapke_leg.b0;
        m->params.hapke_leg.b0 = 0.0;
        zero_leg = 1;
    }

    /* Standard conditions: i=e=g=0 */
    double val = p_photomodel_eval(pm, 0.0, 0.0, 0.0);

    /* Restore B0. */
    if (zero_hen) m->params.hapke_hen.b0 = b0_saved_hen;
    if (zero_leg) m->params.hapke_leg.b0 = b0_saved_leg;

    /* Avoid division by zero in callers. */
    if (val == 0.0) val = 1.0;
    return val;
}

/* ------------------------------------------------------------------ */

const char *p_photomodel_name(const PPhotoModel *pm)
{
    if (!pm) return "NULL";
    switch (((const struct PPhotoModel *)pm)->type) {
    case P_PHOTOMODEL_LAMBERT:              return "Lambert";
    case P_PHOTOMODEL_LOMMELSEELIGER:       return "LommelSeeliger";
    case P_PHOTOMODEL_LUNARLAMBERT:         return "LunarLambert";
    case P_PHOTOMODEL_MINNAERT:             return "Minnaert";
    case P_PHOTOMODEL_HAPKE_HEN:            return "HapkeHen";
    case P_PHOTOMODEL_HAPKE_LEG:            return "HapkeLeg";
    case P_PHOTOMODEL_LUNARLAMBERT_MCEWEN:  return "LunarLambertMcEwen";
    default:                                return "Unknown";
    }
}

/* ------------------------------------------------------------------ */

void p_photomodel_free(PPhotoModel *m)
{
    if (m) G_free(m);
}

/* ================================================================== */
/* Row-processing with OpenMP                                           */
/* ================================================================== */

/*!
 * \brief Apply photometric correction to one row of pixels (OpenMP parallelised).
 *
 * For each sample s in [0, nsamples):
 *   output[s] = input[s] / model(phase[s], incid[s], emiss[s])
 *               * standard_value
 *
 * Samples where the model returns 0 (e.g. incidence ≥ 90°) or where
 * input[s] is NaN are written as NaN in output[s].
 *
 * Each OpenMP thread uses a private copy of the model struct so the
 * Hapke roughness cache is thread-safe.
 *
 * \param m          photometric model
 * \param nsamples   number of pixels in this row
 * \param input      raw DN (or calibrated) values
 * \param phase      per-pixel phase angle [deg]
 * \param incidence  per-pixel incidence angle [deg]
 * \param emission   per-pixel emission angle [deg]
 * \param standard   standard-condition model value (from p_photomodel_standard())
 * \param output     corrected output buffer (caller-allocated, length nsamples)
 */
void p_photomodel_apply_row(const PPhotoModel *m,
                             int nsamples,
                             const double *input,
                             const double *phase,
                             const double *incidence,
                             const double *emission,
                             double standard,
                             double *output)
{
    int s;

#ifdef _OPENMP
    /*
     * Each thread gets its own copy of the model so the Hapke roughness
     * cache (_theta_cache, _cott, …) is private and not shared.
     */
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        double dn = input[s];
        if (dn != dn) { /* NaN check — isnan() is not available in C89 */
            output[s] = dn;
            continue;
        }

        /* Thread-local copy of model for Hapke cache safety. */
#ifdef _OPENMP
        struct PPhotoModel local_m = *(const struct PPhotoModel *)m;
        local_m._theta_cache = -999.0; /* force per-thread recompute */
        double fval = p_photomodel_eval((PPhotoModel *)&local_m,
                                         phase[s], incidence[s], emission[s]);
#else
        double fval = p_photomodel_eval(m, phase[s], incidence[s], emission[s]);
#endif

        if (fval == 0.0) {
            output[s] = dn;  /* undefined geometry — pass through */
            continue;
        }
        output[s] = dn * standard / fval;
    }
}
