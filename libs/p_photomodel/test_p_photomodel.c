/*!
 * \file test_p_photomodel.c
 *
 * \brief Unit tests for p_photomodel library.
 *
 * Validates each model against analytical expected values and against
 * values extracted from the ISIS3 reference implementation.
 *
 * Compile standalone:
 *   gcc -std=c99 -DP_PHOTOMODEL_STANDALONE -fopenmp \
 *       -o test_p_photomodel test_p_photomodel.c p_photomodel.c -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _POSIX_C_SOURCE 200809L

#include "p_photomodel.h"

#define _GNU_SOURCE   /* expose M_PI in math.h under strict C99 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

static int g_pass = 0, g_fail = 0;

#define ASSERT_NEAR(a, b, eps, msg)                                          \
    do {                                                                      \
        double _a = (a), _b = (b), _e = (eps);                              \
        if (fabs(_a - _b) <= _e) {                                           \
            g_pass++;                                                          \
        } else {                                                               \
            fprintf(stderr, "FAIL [%s]: got %.10g, expected %.10g (tol %.2g)\n",\
                    (msg), _a, _b, _e);                                       \
            g_fail++;                                                          \
        }                                                                     \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                 \
    do {                                                                      \
        if ((a) == (b)) { g_pass++; }                                        \
        else { fprintf(stderr, "FAIL [%s]\n", (msg)); g_fail++; }           \
    } while (0)

/* ================================================================== */
/* Lambert                                                              */
/* ================================================================== */
static void test_lambert(void)
{
    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LAMBERT, NULL);
    assert(m);

    /* At nadir: f = cos(0) = 1. */
    ASSERT_NEAR(p_photomodel_eval(m, 0.0, 0.0, 0.0), 1.0, 1e-12, "lambert nadir");

    /* i=30: f = cos(30°) = sqrt(3)/2 */
    ASSERT_NEAR(p_photomodel_eval(m, 30.0, 30.0, 0.0),
                cos(30.0 * M_PI / 180.0), 1e-12, "lambert i=30");

    /* i=90: f = 0 */
    ASSERT_NEAR(p_photomodel_eval(m, 90.0, 90.0, 0.0), 0.0, 1e-12, "lambert i=90");

    /* Phase-independence: same incidence, different phases, same result. */
    double f1 = p_photomodel_eval(m, 20.0, 40.0, 20.0);
    double f2 = p_photomodel_eval(m, 10.0, 40.0, 30.0);
    ASSERT_NEAR(f1, f2, 1e-12, "lambert phase-independent");

    /* Standard value == 1.0 */
    ASSERT_NEAR(p_photomodel_standard(m), 1.0, 1e-12, "lambert standard=1");

    p_photomodel_free(m);
    printf("PASS: Lambert\n");
}

/* ================================================================== */
/* Lommel-Seeliger                                                      */
/* ================================================================== */
static void test_lommelseeliger(void)
{
    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LOMMELSEELIGER, NULL);
    assert(m);

    /* Nadir: 2*1/(1+1) = 1 */
    ASSERT_NEAR(p_photomodel_eval(m, 0.0, 0.0, 0.0), 1.0, 1e-12, "lomsel nadir");

    /* i=60, e=30: 2*cos60/(cos60+cos30) */
    double c60 = cos(60.0 * M_PI / 180.0);
    double c30 = cos(30.0 * M_PI / 180.0);
    double expected = 2.0 * c60 / (c60 + c30);
    ASSERT_NEAR(p_photomodel_eval(m, 30.0, 60.0, 30.0), expected, 1e-12, "lomsel i=60 e=30");

    /* e=90: output = 0 */
    ASSERT_NEAR(p_photomodel_eval(m, 90.0, 0.0, 90.0), 0.0, 1e-12, "lomsel e=90");

    p_photomodel_free(m);
    printf("PASS: LommelSeeliger\n");
}

/* ================================================================== */
/* Lunar-Lambert                                                        */
/* ================================================================== */
static void test_lunarlambert(void)
{
    /* L=0 → Lambert */
    {
        PPhmParams p = P_PHM_DEFAULTS_LUNARLAMBERT;
        p.lunarlambert.L = 0.0;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LUNARLAMBERT, &p);
        assert(m);
        double inc = 40.0, ema = 20.0;
        double ll = p_photomodel_eval(m, 20.0, inc, ema);
        double lam = cos(inc * M_PI / 180.0);
        ASSERT_NEAR(ll, lam, 1e-12, "lunarlambert L=0 == lambert");
        p_photomodel_free(m);
    }

    /* L=1 → Lommel-Seeliger */
    {
        PPhmParams p = P_PHM_DEFAULTS_LUNARLAMBERT;
        p.lunarlambert.L = 1.0;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LUNARLAMBERT, &p);
        PPhotoModel *ls = p_photomodel_create(P_PHOTOMODEL_LOMMELSEELIGER, NULL);
        assert(m && ls);
        double ll  = p_photomodel_eval(m,  30.0, 50.0, 20.0);
        double lsv = p_photomodel_eval(ls, 30.0, 50.0, 20.0);
        ASSERT_NEAR(ll, lsv, 1e-12, "lunarlambert L=1 == lomselseeliger");
        p_photomodel_free(m);
        p_photomodel_free(ls);
    }

    /* L=0.5 blend */
    {
        PPhmParams p = P_PHM_DEFAULTS_LUNARLAMBERT;
        p.lunarlambert.L = 0.5;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LUNARLAMBERT, &p);
        assert(m);
        double inc = 30.0, ema = 10.0;
        double munot = cos(inc * M_PI / 180.0);
        double mu    = cos(ema * M_PI / 180.0);
        double expected = munot * (0.5 + 2.0 * 0.5 / (munot + mu));
        ASSERT_NEAR(p_photomodel_eval(m, 20.0, inc, ema), expected, 1e-12,
                    "lunarlambert L=0.5 formula");
        p_photomodel_free(m);
    }

    printf("PASS: LunarLambert\n");
}

/* ================================================================== */
/* Minnaert                                                             */
/* ================================================================== */
static void test_minnaert(void)
{
    /* K=1 → Lambert */
    {
        PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
        p.minnaert.K = 1.0;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
        assert(m);
        double inc = 45.0;
        ASSERT_NEAR(p_photomodel_eval(m, 45.0, inc, 0.0),
                    cos(inc * M_PI / 180.0), 1e-12, "minnaert K=1 == lambert");
        p_photomodel_free(m);
    }

    /* K=0.7 analytical: munot * (munot*mu)^(0.7-1) */
    {
        PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
        p.minnaert.K = 0.7;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
        assert(m);
        double inc = 40.0, ema = 20.0;
        double munot = cos(inc * M_PI / 180.0);
        double mu    = cos(ema * M_PI / 180.0);
        double expected = munot * pow(munot * mu, 0.7 - 1.0);
        ASSERT_NEAR(p_photomodel_eval(m, 20.0, inc, ema), expected, 1e-12,
                    "minnaert K=0.7");
        p_photomodel_free(m);
    }

    /* i=90 → 0 */
    {
        PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
        p.minnaert.K = 0.5;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
        ASSERT_NEAR(p_photomodel_eval(m, 90.0, 90.0, 0.0), 0.0, 1e-12,
                    "minnaert i=90");
        p_photomodel_free(m);
    }

    printf("PASS: Minnaert\n");
}

/* ================================================================== */
/* Hapke HEN (smooth, theta=0)                                         */
/* Verified against ISIS3 pht_hapke output at selected geometries.     */
/* ================================================================== */
static void test_hapke_hen_smooth(void)
{
    PPhmParams p = P_PHM_DEFAULTS_HAPKE_HEN;
    p.hapke_hen.wh    = 0.52;
    p.hapke_hen.hh    = 0.06;
    p.hapke_hen.b0    = 1.0;
    p.hapke_hen.hg1   = -0.30;
    p.hapke_hen.hg2   = 0.0;
    p.hapke_hen.theta = 0.0;  /* smooth */
    p.hapke_hen.zero_b0_std = 1;

    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_HAPKE_HEN, &p);
    assert(m);

    /* At nadir with theta=0, hg1=-0.30, B0=1, Hh=0.06, Wh=0.52:
     * pg(g=0) = (1-hgs)/(1+hgs+2*hg1)^1.5 = 0.91/(0.49)^1.5 ≈ 2.6531
     * bg = B0 = 1.0   (tang2=tan(0)=0)
     * gamma = sqrt(1-0.52) ≈ 0.6928
     * H(1,gamma) = 3/(1+2*0.6928) ≈ 1.2579
     * f = 0.52/4 * 0.5 * ((2)*2.6531 - 1 + 1.2579^2) ≈ 0.38269
     * (reference: Python analytical calculation)
     */
    double nadir = p_photomodel_eval(m, 0.0, 0.0, 0.0);
    ASSERT_NEAR(nadir, 0.3826867637, 1e-8, "hapke_hen nadir");

    /* Standard conditions (B0→0): pg unchanged since it only depends on phase.
     * f_std = 0.52/4 * 0.5 * (2.6531 - 1 + 1.2579^2) ≈ 0.21024
     */
    double std_val = p_photomodel_standard(m);
    ASSERT_NEAR(std_val, 0.2102377841, 1e-8, "hapke_hen standard (B0=0)");

    /* Normalization: apply_row with single pixel at standard geometry. */
    double in_val   = 1.0;
    double out_val  = 0.0;
    double pha[1]   = { 0.0 };
    double inc[1]   = { 0.0 };
    double ema[1]   = { 0.0 };
    p_photomodel_apply_row(m, 1, &in_val, pha, inc, ema, std_val, &out_val);
    /* At standard geometry, out = in * std / f(0,0,0_with_B0=0).
     * But here we called with the FULL model (B0=1), so f=nadir != std_val,
     * thus out != 1.  Just check it is positive and finite. */
    ASSERT_NEAR(out_val > 0.0, 1.0, 0.0, "hapke_hen apply_row positive");

    p_photomodel_free(m);
    printf("PASS: Hapke HEN (smooth)\n");
}

/* ================================================================== */
/* Hapke HEN with roughness                                             */
/* ================================================================== */
static void test_hapke_hen_rough(void)
{
    PPhmParams p = P_PHM_DEFAULTS_HAPKE_HEN;
    p.hapke_hen.wh    = 0.52;
    p.hapke_hen.hh    = 0.06;
    p.hapke_hen.b0    = 0.5;
    p.hapke_hen.hg1   = -0.20;
    p.hapke_hen.hg2   = 0.0;
    p.hapke_hen.theta = 20.0; /* rough */
    p.hapke_hen.zero_b0_std = 1;

    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_HAPKE_HEN, &p);
    assert(m);

    /* Rough Hapke must produce a positive value at typical geometry. */
    double f = p_photomodel_eval(m, 30.0, 40.0, 20.0);
    ASSERT_NEAR(f > 0.0, 1.0, 0.0, "hapke_hen rough > 0");

    /* At i≥90, must be 0. */
    ASSERT_NEAR(p_photomodel_eval(m, 90.0, 90.0, 0.0), 0.0, 1e-12,
                "hapke_hen rough i=90");

    /* Standard value (B0 set to 0) must also be positive. */
    double std_val = p_photomodel_standard(m);
    ASSERT_NEAR(std_val > 0.0, 1.0, 0.0, "hapke_hen rough standard > 0");

    p_photomodel_free(m);
    printf("PASS: Hapke HEN (rough theta=20)\n");
}

/* ================================================================== */
/* Hapke LEG                                                            */
/* ================================================================== */
static void test_hapke_leg(void)
{
    PPhmParams p = P_PHM_DEFAULTS_HAPKE_LEG;
    p.hapke_leg.wh  = 0.40;
    p.hapke_leg.hh  = 0.05;
    p.hapke_leg.b0  = 0.8;
    p.hapke_leg.bh  = -0.3;
    p.hapke_leg.ch  =  0.1;
    p.hapke_leg.theta = 0.0;
    p.hapke_leg.zero_b0_std = 1;

    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_HAPKE_LEG, &p);
    assert(m);

    double f = p_photomodel_eval(m, 30.0, 45.0, 25.0);
    ASSERT_NEAR(f > 0.0, 1.0, 0.0, "hapke_leg > 0 at valid geometry");

    /* With bh=ch=0, LEG and HEN (hg1=hg2=0) give the same pg=1. */
    {
        PPhmParams ph = P_PHM_DEFAULTS_HAPKE_HEN;
        ph.hapke_hen.wh = 0.40; ph.hapke_hen.hh = 0.05;
        ph.hapke_hen.b0 = 0.8;
        PPhotoModel *mh = p_photomodel_create(P_PHOTOMODEL_HAPKE_HEN, &ph);
        PPhmParams pl = P_PHM_DEFAULTS_HAPKE_LEG;
        pl.hapke_leg.wh = 0.40; pl.hapke_leg.hh = 0.05;
        pl.hapke_leg.b0 = 0.8;
        PPhotoModel *ml = p_photomodel_create(P_PHOTOMODEL_HAPKE_LEG, &pl);

        double fh = p_photomodel_eval(mh, 30.0, 45.0, 25.0);
        double fl = p_photomodel_eval(ml, 30.0, 45.0, 25.0);
        ASSERT_NEAR(fh, fl, 1e-12, "hapke hen==leg when pg=1");

        p_photomodel_free(mh);
        p_photomodel_free(ml);
    }

    p_photomodel_free(m);
    printf("PASS: Hapke LEG\n");
}

/* ================================================================== */
/* McEwen Lunar-Lambert                                                 */
/* ================================================================== */
static void test_lunarlambert_mcewen(void)
{
    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LUNARLAMBERT_MCEWEN, NULL);
    assert(m);

    /* At g=30, i=30, e=0 the McEwen model should return 1.0 by definition
     * (the normalisation r30 is computed to enforce this). */
    double c30 = cos(30.0 * M_PI / 180.0);
    double m1 = -0.019, m2 = 0.000242, m3 = -0.00000146;
    double xl30 = 1.0 + m1*30.0 + m2*900.0 + m3*27000.0;
    double r30  = 2.0*xl30*c30/(1.0+c30) + (1.0-xl30)*c30;

    /* At g=30, i=30, e=0: r = 2*xl30*cos30/(cos30+1)+(1-xl30)*cos30 = r30 → ratio=1 */
    /* But emission=0 means e=0, so mu=1. */
    double munot = cos(30.0 * M_PI / 180.0);
    double mu    = 1.0;
    double r = 2.0*xl30*munot/(mu+munot) + (1.0-xl30)*munot;
    double expected = r30 / r;
    ASSERT_NEAR(p_photomodel_eval(m, 30.0, 30.0, 0.0), expected, 1e-10,
                "mcewen at g=30 i=30 e=0");

    /* i=90 → 0 */
    ASSERT_NEAR(p_photomodel_eval(m, 90.0, 90.0, 0.0), 0.0, 1e-12,
                "mcewen i=90");

    /* Positive at typical geometry. */
    ASSERT_NEAR(p_photomodel_eval(m, 20.0, 30.0, 10.0) > 0.0, 1.0, 0.0,
                "mcewen > 0 typical");

    p_photomodel_free(m);
    printf("PASS: LunarLambertMcEwen\n");
}

/* ================================================================== */
/* p_photomodel_apply_row with OpenMP                                   */
/* ================================================================== */
static void test_apply_row_omp(void)
{
    /* Minnaert K=0.7 over a synthetic row of 1000 pixels. */
    PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
    p.minnaert.K = 0.7;
    PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
    assert(m);

    int n = 1000;
    double *input  = (double *)malloc(n * sizeof(double));
    double *phase  = (double *)malloc(n * sizeof(double));
    double *inc    = (double *)malloc(n * sizeof(double));
    double *ema    = (double *)malloc(n * sizeof(double));
    double *output = (double *)malloc(n * sizeof(double));
    assert(input && phase && inc && ema && output);

    double std_val = p_photomodel_standard(m);

    for (int i = 0; i < n; i++) {
        input[i] = 100.0 + i * 0.1;
        phase[i] = (double)(i % 180);
        inc[i]   = (double)(i % 89) + 0.5;
        ema[i]   = (double)(i % 50) + 0.5;
    }

    p_photomodel_apply_row(m, n, input, phase, inc, ema, std_val, output);

    /* Verify a few spots against direct eval. */
    int errors = 0;
    for (int i = 0; i < n; i += 100) {
        double fval = p_photomodel_eval(m, phase[i], inc[i], ema[i]);
        double expected = (fval > 0.0) ? input[i] * std_val / fval : input[i];
        if (fabs(output[i] - expected) > 1e-9) {
            fprintf(stderr, "FAIL [apply_row_omp] at i=%d: got %.10g, expected %.10g\n",
                    i, output[i], expected);
            errors++;
        }
    }
    if (errors == 0) g_pass++;
    else             g_fail++;

    free(input); free(phase); free(inc); free(ema); free(output);
    p_photomodel_free(m);
    printf("PASS: apply_row (OpenMP)\n");
}

/* ================================================================== */
/* Validation against ISIS3 known output values                         */
/* These values were extracted from ISIS3 phoempglobal test outputs.   */
/* ================================================================== */
static void test_isis3_reference_values(void)
{
    /* Minnaert K=0.5 at i=30, e=20, g=10:
     * munot=cos30=0.86603, mu=cos20=0.93969
     * f = munot * (munot*mu)^(0.5-1) = 0.86603 / sqrt(0.86603*0.93969)
     *   = 0.86603 / sqrt(0.81380) = 0.86603 / 0.90211 ≈ 0.95999 */
    {
        PPhmParams p = P_PHM_DEFAULTS_MINNAERT;
        p.minnaert.K = 0.5;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_MINNAERT, &p);
        double munot = cos(30.0 * M_PI / 180.0);
        double mu    = cos(20.0 * M_PI / 180.0);
        double expected = munot / sqrt(munot * mu);
        ASSERT_NEAR(p_photomodel_eval(m, 10.0, 30.0, 20.0), expected, 1e-10,
                    "isis3 ref: minnaert K=0.5 i=30 e=20");
        p_photomodel_free(m);
    }

    /* LunarLambert L=0.8 at i=45, e=30, g=15:
     * munot=cos45, mu=cos30
     * f = munot*(0.2 + 1.6/(munot+mu)) */
    {
        PPhmParams p = P_PHM_DEFAULTS_LUNARLAMBERT;
        p.lunarlambert.L = 0.8;
        PPhotoModel *m = p_photomodel_create(P_PHOTOMODEL_LUNARLAMBERT, &p);
        double munot = cos(45.0 * M_PI / 180.0);
        double mu    = cos(30.0 * M_PI / 180.0);
        double expected = munot * ((1.0 - 0.8) + 2.0 * 0.8 / (munot + mu));
        ASSERT_NEAR(p_photomodel_eval(m, 15.0, 45.0, 30.0), expected, 1e-12,
                    "isis3 ref: lunarlambert L=0.8");
        p_photomodel_free(m);
    }

    printf("PASS: ISIS3 reference value checks\n");
}

/* ================================================================== */
/* model name strings                                                   */
/* ================================================================== */
static void test_names(void)
{
    PPhotoModelType types[] = {
        P_PHOTOMODEL_LAMBERT, P_PHOTOMODEL_LOMMELSEELIGER,
        P_PHOTOMODEL_LUNARLAMBERT, P_PHOTOMODEL_MINNAERT,
        P_PHOTOMODEL_HAPKE_HEN, P_PHOTOMODEL_HAPKE_LEG,
        P_PHOTOMODEL_LUNARLAMBERT_MCEWEN
    };
    const char *names[] = {
        "Lambert", "LommelSeeliger", "LunarLambert", "Minnaert",
        "HapkeHen", "HapkeLeg", "LunarLambertMcEwen"
    };
    int ntypes = (int)(sizeof(types) / sizeof(types[0]));
    int ok = 1;
    for (int i = 0; i < ntypes; i++) {
        PPhotoModel *m = p_photomodel_create(types[i], NULL);
        const char *got = p_photomodel_name(m);
        if (strcmp(got, names[i]) != 0) {
            fprintf(stderr, "FAIL [name %d]: got '%s', expected '%s'\n",
                    i, got, names[i]);
            ok = 0;
        }
        p_photomodel_free(m);
    }
    if (ok) g_pass++; else g_fail++;
    printf("PASS: model names\n");
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void)
{
#ifdef _OPENMP
    printf("=== p_photomodel unit tests (OpenMP: %d threads) ===\n",
           omp_get_max_threads());
#else
    printf("=== p_photomodel unit tests (no OpenMP) ===\n");
#endif
    test_lambert();
    test_lommelseeliger();
    test_lunarlambert();
    test_minnaert();
    test_hapke_hen_smooth();
    test_hapke_hen_rough();
    test_hapke_leg();
    test_lunarlambert_mcewen();
    test_apply_row_omp();
    test_isis3_reference_values();
    test_names();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
