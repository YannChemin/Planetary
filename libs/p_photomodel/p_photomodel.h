/*!
 * \file p_photomodel.h
 *
 * \brief Planetary library - photometric models for surface reflectance.
 *
 * Provides a pure-C implementation of the photometric models used in ISIS3
 * for planetary surface reflectance correction.  All models take angles in
 * degrees and return a dimensionless reflectance factor f(i,e,g) that
 * equals 1.0 at standard conditions (i=e=g=0) after normalization.
 *
 * Models implemented (matching ISIS3 PhotoModel subclasses)
 * ---------------------------------------------------------
 *  P_PHOTOMODEL_LAMBERT          - Lambertian: f = cos(i)
 *  P_PHOTOMODEL_LOMMELSEELIGER   - Lommel-Seeliger: f = 2cos(i)/(cos(i)+cos(e))
 *  P_PHOTOMODEL_LUNARLAMBERT     - Lunar-Lambert blend (L param); L=0→Lambert, L=1→LS
 *  P_PHOTOMODEL_MINNAERT         - Minnaert: f = cos(i)·(cos(i)·cos(e))^(K-1)
 *  P_PHOTOMODEL_HAPKE_HEN        - Full Hapke (1981) with Henyey-Greenstein phase fn
 *  P_PHOTOMODEL_HAPKE_LEG        - Full Hapke with Legendre polynomial phase fn
 *  P_PHOTOMODEL_LUNARLAMBERT_MCEWEN - McEwen (1991) polynomial-scaled lunar-Lambert
 *
 * Usage example
 * -------------
 * \code
 *   PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
 *   p.minnaert.K = 0.7;
 *   PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
 *   double f = p_photomodel_eval(m, phase_deg, incid_deg, emiss_deg);
 *   p_photomodel_free(m);
 * \endcode
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 *         Algorithm sources: ISIS3 PhotoModel hierarchy (USGS Astrogeology)
 */

#ifndef GRASS_P_PHOTOMODEL_H
#define GRASS_P_PHOTOMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Model identifiers                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    P_PHOTOMODEL_LAMBERT           = 0,
    P_PHOTOMODEL_LOMMELSEELIGER    = 1,
    P_PHOTOMODEL_LUNARLAMBERT      = 2,
    P_PHOTOMODEL_MINNAERT          = 3,
    P_PHOTOMODEL_HAPKE_HEN         = 4,  /*!< Henyey-Greenstein phase fn  */
    P_PHOTOMODEL_HAPKE_LEG         = 5,  /*!< Legendre polynomial phase fn */
    P_PHOTOMODEL_LUNARLAMBERT_MCEWEN = 6
} PPhotoModelType;

/* ------------------------------------------------------------------ */
/* Per-model parameter structs                                          */
/* ------------------------------------------------------------------ */

/*! Lambert: no free parameters. */
typedef struct { int _dummy; } PPhmLambert;

/*! Lommel-Seeliger: no free parameters. */
typedef struct { int _dummy; } PPhmLommelSeeliger;

/*!
 * Lunar-Lambert mixing weight L.
 *   L=0  →  Lambertian
 *   L=1  →  Lommel-Seeliger ("lunar")
 * Typical lunar value: L≈0.9–1.0; Mars ≈0.5.
 */
typedef struct {
    double L; /*!< mixing weight [0, 1], default 1.0 */
} PPhmLunarLambert;

/*!
 * Minnaert limb-darkening exponent K.
 *   K=1.0  →  Lambertian
 *   K=0.5  →  "lunar-like" (nearly uniform disk)
 */
typedef struct {
    double K; /*!< exponent [0.5, 1.0 typical], default 1.0 */
} PPhmMinnaert;

/*!
 * Hapke (1981/1984) model with Henyey-Greenstein two-component phase fn.
 *
 * Parameters follow Hapke (1981) Table 1 and Hapke (1984) roughness paper.
 *
 * HapkeHen phase function:
 *   p(g) = (1-hg2)·(1-hg1²)/(1+hg1²+2·hg1·cos g)^1.5
 *         + hg2·(1-hg1²)/(1+hg1²-2·hg1·cos g)^1.5
 */
typedef struct {
    double wh;           /*!< single-scattering albedo ω  (0, 1],  default 0.5   */
    double hh;           /*!< opposition-surge width h    ≥0,       default 0.0   */
    double b0;           /*!< opposition-surge amplitude B₀ ≥0,     default 0.0   */
    double hg1;          /*!< HG asymmetry 1st coeff     (-1,1),    default 0.0   */
    double hg2;          /*!< HG 2nd component weight    [0,1],     default 0.0   */
    double theta;        /*!< macroscopic roughness angle Θ [0,90°], default 0.0  */
    int    zero_b0_std;  /*!< set B0=0 at standard conditions (1=yes), default 1  */
} PPhmHapkeHen;

/*!
 * Hapke model with Legendre polynomial phase fn.
 *   p(g) = 1 + bh·cos g + ch·(1.5·cos²g - 0.5)
 */
typedef struct {
    double wh;           /*!< single-scattering albedo ω  (0, 1],  default 0.5   */
    double hh;           /*!< opposition-surge width h    ≥0,       default 0.0   */
    double b0;           /*!< opposition-surge amplitude B₀ ≥0,     default 0.0   */
    double bh;           /*!< Legendre coeff b1           [-1,1],   default 0.0   */
    double ch;           /*!< Legendre coeff b2           [-1,1],   default 0.0   */
    double theta;        /*!< macroscopic roughness angle Θ [0,90°], default 0.0  */
    int    zero_b0_std;  /*!< set B0=0 at standard conditions (1=yes), default 1  */
} PPhmHapkeLeg;

/*!
 * McEwen (1991) polynomial-scaled Lunar-Lambert.
 * L(g) = 1 + M1·g + M2·g² + M3·g³  (g in degrees)
 * Normalised so that f=1 at g=30° (McEwen calibration geometry).
 * Default coefficients fit the lunar near-infrared.
 */
typedef struct {
    double m1; /*!< polynomial coeff 1, default -0.019    */
    double m2; /*!< polynomial coeff 2, default +0.000242 */
    double m3; /*!< polynomial coeff 3, default -1.46e-6  */
} PPhmLunarLambertMcEwen;

/* ------------------------------------------------------------------ */
/* Union of all parameter sets                                          */
/* ------------------------------------------------------------------ */

typedef union {
    PPhmLambert           lambert;
    PPhmLommelSeeliger    lommelseeliger;
    PPhmLunarLambert      lunarlambert;
    PPhmMinnaert          minnaert;
    PPhmHapkeHen          hapke_hen;
    PPhmHapkeLeg          hapke_leg;
    PPhmLunarLambertMcEwen lunarlambert_mcewen;
} PPhmParams;

/* ------------------------------------------------------------------ */
/* Default parameter initialisers (use as: PPhmParams p = P_PHM_DEFAULTS_HAPKE_HEN;) */
/* ------------------------------------------------------------------ */

#define P_PHM_DEFAULTS_LAMBERT \
    { .lambert = { 0 } }

#define P_PHM_DEFAULTS_LOMMELSEELIGER \
    { .lommelseeliger = { 0 } }

#define P_PHM_DEFAULTS_LUNARLAMBERT \
    { .lunarlambert = { .L = 1.0 } }

#define P_PHM_DEFAULTS_MINNAERT \
    { .minnaert = { .K = 1.0 } }

#define P_PHM_DEFAULTS_HAPKE_HEN \
    { .hapke_hen = { .wh=0.5, .hh=0.0, .b0=0.0, \
                     .hg1=0.0, .hg2=0.0, .theta=0.0, .zero_b0_std=1 } }

#define P_PHM_DEFAULTS_HAPKE_LEG \
    { .hapke_leg = { .wh=0.5, .hh=0.0, .b0=0.0, \
                     .bh=0.0, .ch=0.0, .theta=0.0, .zero_b0_std=1 } }

#define P_PHM_DEFAULTS_LUNARLAMBERT_MCEWEN \
    { .lunarlambert_mcewen = { .m1=-0.019, .m2=0.000242, .m3=-0.00000146 } }

/* ------------------------------------------------------------------ */
/* Opaque model handle                                                  */
/* ------------------------------------------------------------------ */

/*! Opaque photometric model handle returned by p_photomodel_create(). */
typedef struct PPhotoModel PPhotoModel;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/*!
 * \brief Create a photometric model.
 *
 * \param type    model identifier (P_PHOTOMODEL_*)
 * \param params  parameter union; may be NULL to use built-in defaults
 * \return heap-allocated PPhotoModel*, or NULL on invalid parameters
 */
PPhotoModel *p_photomodel_create(PPhotoModelType type, const PPhmParams *params);

/*!
 * \brief Evaluate the photometric function.
 *
 * Returns the raw (un-normalised) reflectance factor f(phase, incidence, emission).
 * All angles are in degrees.  Returns 0.0 for geometrically invalid inputs
 * (incidence ≥ 90° or emission ≥ 90°).
 *
 * \param m             open PPhotoModel
 * \param phase_deg     phase angle g  [0, 180]
 * \param incidence_deg incidence angle i  [0, 90)
 * \param emission_deg  emission angle e  [0, 90)
 * \return reflectance factor (dimensionless)
 */
double p_photomodel_eval(const PPhotoModel *m,
                          double phase_deg,
                          double incidence_deg,
                          double emission_deg);

/*!
 * \brief Evaluate the model at nadir-standard conditions (i=e=g=0).
 *
 * Used as the denominator for normalisation so that corrected DN maps to
 * a surface whose standard-condition brightness equals the original.
 * For Hapke models with B0>0, B0 is zeroed during this call (matching
 * ISIS3 SetStandardConditions behaviour when zero_b0_std=1).
 *
 * \return f(0, 0, 0)  (always > 0 for valid models)
 */
double p_photomodel_standard(const PPhotoModel *m);

/*!
 * \brief Return a human-readable model name string.
 */
const char *p_photomodel_name(const PPhotoModel *m);

/*!
 * \brief Free a PPhotoModel created by p_photomodel_create().
 */
void p_photomodel_free(PPhotoModel *m);

/*!
 * \brief Apply photometric correction to one raster row (OpenMP parallelised).
 *
 * Corrects each pixel:  output[s] = input[s] * standard / f(phase,inc,emiss)
 * where standard = p_photomodel_standard(m).
 * Pixels where f==0 or input is NaN are passed through unchanged.
 *
 * Thread-safe: each OpenMP thread uses a private copy of the model's
 * roughness cache, so this is safe to call for all bands concurrently.
 *
 * \param m          photometric model
 * \param nsamples   number of pixels in this row
 * \param input      raw input values (DCELL / double)
 * \param phase      per-pixel phase angle array [deg], length nsamples
 * \param incidence  per-pixel incidence angle array [deg]
 * \param emission   per-pixel emission angle array [deg]
 * \param standard   p_photomodel_standard(m)
 * \param output     corrected output buffer (caller-allocated, length nsamples)
 */
void p_photomodel_apply_row(const PPhotoModel *m,
                             int nsamples,
                             const double *input,
                             const double *phase,
                             const double *incidence,
                             const double *emission,
                             double standard,
                             double *output);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_PHOTOMODEL_H */
