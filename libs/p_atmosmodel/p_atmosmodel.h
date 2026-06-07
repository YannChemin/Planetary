/*!
 * \file p_atmosmodel.h
 *
 * \brief Planetary library - atmospheric scattering models for
 *        surface reflectance correction.
 *
 * Provides a pure-C implementation of the four ISIS3 atmospheric scattering
 * models used in planetary photometric correction.  All models solve for the
 * five atmospheric terms needed to separate surface and sky contributions from
 * a measured planetary radiance.
 *
 * Models implemented (matching ISIS3 AtmosModel subclasses)
 * ---------------------------------------------------------
 *  P_ATMOSMODEL_ISOTROPIC1  — 1st-order isotropic scattering  (Chandrasekar)
 *  P_ATMOSMODEL_ISOTROPIC2  — 2nd-order isotropic scattering  (more accurate)
 *  P_ATMOSMODEL_ANISOTROPIC1 — 1st-order P1 Legendre phase fn
 *  P_ATMOSMODEL_ANISOTROPIC2 — 2nd-order P1 Legendre phase fn (most accurate)
 *
 * All four models compute five output quantities:
 *  pstd   — pure atmospheric-scattering term
 *  trans  — total transmission of surface-reflected light through atmosphere
 *  trans0 — unscattered-only transmission (Beer's law)
 *  sbar   — diffuse sky illumination of the ground
 *  transs — transmission used for shadow modelling
 *
 * Full correction formula (applied by callers such as p.photomet):
 *   P = pstd + trans  * rho * Ah * munot / (1 - rho * Ab * sbar)
 *            + trans0 * rho * (Psurf - Ah * munot)
 * where rho = surface_albedo / reference_albedo, Ah = directional hemispheric
 * albedo (caller computes by integrating the photometric model), Ab =
 * bi-hemispheric albedo (trapezoid rule over Ah table), Psurf = photometric
 * model value, munot = cos(incidence).
 *
 * p_atmosmodel_apply() encapsulates this formula for convenience.
 *
 * Usage example
 * -------------
 * \code
 *   PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
 *   p.tau  = 0.28;   // Mars atmosphere optical depth
 *   p.wha  = 0.95;   // atmospheric single-scatter albedo
 *   p.hnorm = 0.05;  // shell thickness / planet radius
 *
 *   PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
 *
 *   PAtmResult r;
 *   p_atmosmodel_eval(m, phase, incidence, emission, &r);
 *   double corrected = p_atmosmodel_apply(r.pstd, r.trans, r.trans0,
 *                                          r.sbar, rho, Ah, Ab, Psurf, munot);
 *   p_atmosmodel_free(m);
 * \endcode
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 *         Algorithm sources: ISIS3 AtmosModel hierarchy (USGS Astrogeology,
 *         original authors Randy Kirk, Janet Barrett, K Teal Thompson)
 */

#ifndef GRASS_P_ATMOSMODEL_H
#define GRASS_P_ATMOSMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Model identifiers                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    P_ATMOSMODEL_ISOTROPIC1   = 0, /*!< 1st-order isotropic scattering   */
    P_ATMOSMODEL_ISOTROPIC2   = 1, /*!< 2nd-order isotropic scattering   */
    P_ATMOSMODEL_ANISOTROPIC1 = 2, /*!< 1st-order P1 Legendre phase fn   */
    P_ATMOSMODEL_ANISOTROPIC2 = 3  /*!< 2nd-order P1 Legendre phase fn   */
} PAtmosModelType;

/* ------------------------------------------------------------------ */
/* Parameters (shared by all four models)                              */
/* ------------------------------------------------------------------ */

/*!
 * \brief Atmospheric model parameters.
 *
 * All four models share the same parameter set; bha is only used by
 * the two Anisotropic models.
 */
typedef struct {
    double tau;   /*!< Optical depth of the atmosphere          (>= 0, default 0.28) */
    double wha;   /*!< Single-scatter albedo of atmosphere dust (0,1], default 0.95  */
    double hnorm; /*!< Shell thickness / planet radius          (>= 0, default 0.05) */
    double tauref;/*!< Reference tau (standard conditions)             default 0.0   */
    double bha;   /*!< Anisotropy parameter B for P1 phase fn   [-1,1], default 0.85 */
                  /*!< (Anisotropic models only; ignored by Isotropic models)         */
} PAtmParams;

/* ------------------------------------------------------------------ */
/* Output result struct                                                 */
/* ------------------------------------------------------------------ */

/*!
 * \brief Five atmospheric scattering terms returned by p_atmosmodel_eval().
 */
typedef struct {
    double pstd;   /*!< Pure atmospheric-scattering term                            */
    double trans;  /*!< Total transmission of surface-reflected light                */
    double trans0; /*!< Unscattered-only (Beer's law) transmission                  */
    double sbar;   /*!< Diffuse sky illumination of the ground                      */
    double transs; /*!< Transmission for shadow modelling                           */
} PAtmResult;

/* ------------------------------------------------------------------ */
/* Default parameter initialisers                                       */
/* ------------------------------------------------------------------ */

#define P_ATM_DEFAULTS_ISOTROPIC1 \
    { .tau=0.28, .wha=0.95, .hnorm=0.05, .tauref=0.0, .bha=0.85 }

#define P_ATM_DEFAULTS_ISOTROPIC2 \
    { .tau=0.28, .wha=0.95, .hnorm=0.05, .tauref=0.0, .bha=0.85 }

#define P_ATM_DEFAULTS_ANISOTROPIC1 \
    { .tau=0.28, .wha=0.95, .hnorm=0.05, .tauref=0.0, .bha=0.85 }

#define P_ATM_DEFAULTS_ANISOTROPIC2 \
    { .tau=0.28, .wha=0.95, .hnorm=0.05, .tauref=0.0, .bha=0.85 }

/* ------------------------------------------------------------------ */
/* Opaque model handle                                                  */
/* ------------------------------------------------------------------ */

/*! Opaque atmospheric model handle. */
typedef struct PAtmosModel PAtmosModel;

/* ================================================================== */
/* API                                                                  */
/* ================================================================== */

/*!
 * \brief Create an atmospheric model.
 *
 * \param type    model identifier (P_ATMOSMODEL_*)
 * \param params  parameters; may be NULL to use built-in defaults
 * \return heap-allocated PAtmosModel*, or NULL on invalid parameters
 */
PAtmosModel *p_atmosmodel_create(PAtmosModelType type, const PAtmParams *params);

/*!
 * \brief Evaluate the atmospheric model at a given geometry.
 *
 * Writes all five atmospheric terms into *result.
 * When tau == 0 the function sets pstd=0, trans=trans0=transs=1, sbar=0 and
 * returns immediately (vacuum: no atmospheric correction needed).
 *
 * Expensive tau-dependent intermediate quantities are cached inside the model
 * handle and recomputed only when tau or wha changes between calls.
 *
 * \param m           open PAtmosModel
 * \param phase_deg   phase angle [deg]
 * \param inc_deg     incidence angle [deg]
 * \param ema_deg     emission angle [deg]
 * \param result      output (caller-allocated PAtmResult)
 * \return 0 on success, -1 if geometry is invalid
 */
int p_atmosmodel_eval(PAtmosModel *m,
                       double phase_deg, double inc_deg, double ema_deg,
                       PAtmResult *result);

/*!
 * \brief Apply the full atmospheric correction formula.
 *
 * Combines the five atmospheric terms with the photometric model output to
 * produce the corrected reflectance:
 *
 *   P = pstd + trans  * rho * Ah * munot / (1 - rho * Ab * sbar)
 *            + trans0 * rho * (Psurf - Ah * munot)
 *
 * where Ah and Ab are the directional and bi-hemispheric albedos obtained by
 * integrating the photometric model (caller's responsibility).
 *
 * \param pstd    pure atmospheric scattering term (from PAtmResult)
 * \param trans   total transmission (from PAtmResult)
 * \param trans0  unscattered transmission (from PAtmResult)
 * \param sbar    sky illumination (from PAtmResult)
 * \param rho     surface albedo / reference albedo ratio
 * \param Ah      directional hemispheric albedo at current incidence
 * \param Ab      bi-hemispheric albedo
 * \param Psurf   surface photometric function value
 * \param munot   cos(incidence)
 * \return corrected radiance factor
 */
double p_atmosmodel_apply(double pstd, double trans, double trans0,
                           double sbar, double rho, double Ah, double Ab,
                           double Psurf, double munot);

/*!
 * \brief Apply correction over a full raster row (OpenMP parallelised).
 *
 * Evaluates the atmospheric model and applies the correction for each of the
 * nsamples pixels in the row.  Each pixel has its own geometry (phase, inc,
 * ema) and surface photometric value (psurf).  The Ah and Ab arrays may each
 * be a single scalar (length-1 array, same value for all pixels) or
 * per-pixel arrays (length nsamples) depending on how they were computed.
 *
 * The function is thread-safe: each OpenMP thread uses a private copy of the
 * model's cached state.
 *
 * \param m          atmospheric model
 * \param nsamples   row width in pixels
 * \param input      raw/calibrated DN values (length nsamples)
 * \param psurf      per-pixel photometric function values
 * \param phase      per-pixel phase angle [deg]
 * \param incidence  per-pixel incidence angle [deg]
 * \param emission   per-pixel emission angle [deg]
 * \param rho        surface albedo / reference ratio (scalar or per-pixel)
 * \param Ah         directional hemispheric albedo (scalar or per-pixel)
 * \param Ab         bi-hemispheric albedo (scalar)
 * \param output     corrected output (caller-allocated, length nsamples)
 */
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
                             double *output);

/*!
 * \brief Return a human-readable model name string.
 */
const char *p_atmosmodel_name(const PAtmosModel *m);

/*!
 * \brief Free a PAtmosModel created by p_atmosmodel_create().
 */
void p_atmosmodel_free(PAtmosModel *m);

/* ================================================================== */
/* Low-level special-function helpers (exposed for unit testing)        */
/* ================================================================== */

/*!
 * \brief Generalised exponential integral E_n(x).
 *
 * E_n(x) = integral_{1}^{inf} exp(-x*t)/t^n dt
 * Uses Lentz continued-fraction (x>1) or series (0<x<=1).
 *
 * \param n  order (>= 0); n=0 requires x > 0
 * \param x  argument (>= 0, except n=0 or n=1 require x > 0)
 * \return E_n(x), or NaN on domain error
 */
double p_atm_En(unsigned int n, double x);

/*!
 * \brief Exponential integral Ei(x) for x > 0.
 *
 * Ei(x) = -P.V. integral_{-x}^{inf} exp(-t)/t dt
 * Uses power series (small x) or asymptotic series (large x).
 */
double p_atm_Ei(double x);

/*!
 * \brief Chandrasekhar G'_{11} function.
 *
 * Used internally by 2nd-order models to compute f1 at mu=+1.
 */
double p_atm_G11Prime(double tau);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_ATMOSMODEL_H */
