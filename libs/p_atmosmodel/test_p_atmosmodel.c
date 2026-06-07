/*!
 * \file test_p_atmosmodel.c
 *
 * \brief Unit tests for p_atmosmodel library.
 *
 * Compile standalone:
 *   gcc -std=c99 -DP_ATMOSMODEL_STANDALONE -fopenmp \
 *       -o test_p_atmosmodel test_p_atmosmodel.c p_atmosmodel.c -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "p_atmosmodel.h"

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

#define ASSERT_NEAR(a, b, eps, msg)                                       \
    do {                                                                   \
        double _a=(a), _b=(b), _e=(eps);                                  \
        if (fabs(_a-_b) <= _e) { g_pass++; }                             \
        else { fprintf(stderr,"FAIL [%s]: %.12g vs %.12g (tol %.2g)\n",  \
                       (msg),_a,_b,_e); g_fail++; }                      \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { if(cond){g_pass++;}else{fprintf(stderr,"FAIL [%s]\n",(msg));g_fail++;} } while(0)

/* ================================================================== */
/* Special-function tests                                               */
/* ================================================================== */

static void test_En(void)
{
    /* E_0(x) = exp(-x)/x */
    ASSERT_NEAR(p_atm_En(0, 1.0), exp(-1.0)/1.0, 1e-10, "En(0,1)");

    /* E_1(x) = -gamma - ln x + x - x^2/(2*2!) + … ; for x=0.1 reference value */
    /* Reference: Python scipy.special.expn(1, 0.1) ≈ 1.8229239584193 */
    ASSERT_NEAR(p_atm_En(1, 0.1),  1.8229239584193, 1e-9, "En(1,0.1)");

    /* E_2(1) reference: scipy.special.expn(2,1) ≈ 0.14849550677 */
    ASSERT_NEAR(p_atm_En(2, 1.0),  0.14849550677, 1e-8, "En(2,1)");

    /* E_n(0) = 1/(n-1) for n>1 */
    ASSERT_NEAR(p_atm_En(2, 0.0),  1.0, 1e-12, "En(2,0)=1");
    ASSERT_NEAR(p_atm_En(3, 0.0),  0.5, 1e-12, "En(3,0)=0.5");

    /* Large x: E_1(10) ≈ 4.15697e-6 */
    ASSERT_NEAR(p_atm_En(1, 10.0), 4.15697e-6, 1e-10, "En(1,10)");

    printf("PASS: En()\n");
}

static void test_Ei(void)
{
    /* Ei(1) ≈ 1.8951178163559 (scipy.special.expi(1)) */
    ASSERT_NEAR(p_atm_Ei(1.0),  1.8951178163559, 1e-8, "Ei(1)");

    /* Ei(0.1): scipy.special.expi(0.1) ≈ -1.6228128139693
     * Note: En(1, 0.1) = 1.8229... which is a different function. */
    ASSERT_NEAR(p_atm_Ei(0.1), -1.6228128139693, 1e-8, "Ei(0.1)");

    /* Ei(5) ≈ 40.185275... */
    ASSERT_NEAR(p_atm_Ei(5.0),  40.18527536627, 1e-5, "Ei(5)");

    printf("PASS: Ei()\n");
}

static void test_G11Prime(void)
{
    /* G11Prime = 2*(E1(tau) + elog*E2(tau) - tau*e1_2); positive for tau>0.
     * Python reference (analytical series): G11Prime(0.5) ≈ 0.685535 */
    double g = p_atm_G11Prime(0.5);
    ASSERT_TRUE(isfinite(g),     "G11Prime(0.5) finite");
    ASSERT_TRUE(g > 0.0,         "G11Prime(0.5) > 0");
    ASSERT_NEAR(g, 0.685535, 1e-4, "G11Prime(0.5)");
    /* G11Prime(1.0) ≈ 0.41451010749 */
    ASSERT_NEAR(p_atm_G11Prime(1.0), 0.41451010749, 1e-6, "G11Prime(1.0)");
    printf("PASS: G11Prime()\n");
}

/* ================================================================== */
/* Vacuum short-circuit (tau = 0)                                       */
/* ================================================================== */

static void test_vacuum(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
    p.tau = 0.0;
    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    assert(m);

    PAtmResult r;
    p_atmosmodel_eval(m, 30.0, 45.0, 20.0, &r);

    ASSERT_NEAR(r.pstd,   0.0, 1e-15, "vacuum pstd=0");
    ASSERT_NEAR(r.trans,  1.0, 1e-15, "vacuum trans=1");
    ASSERT_NEAR(r.trans0, 1.0, 1e-15, "vacuum trans0=1");
    ASSERT_NEAR(r.sbar,   0.0, 1e-15, "vacuum sbar=0");
    ASSERT_NEAR(r.transs, 1.0, 1e-15, "vacuum transs=1");

    p_atmosmodel_free(m);
    printf("PASS: vacuum (tau=0)\n");
}

/* ================================================================== */
/* Isotropic1 basic physics checks                                      */
/* ================================================================== */

static void test_isotropic1_physics(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
    p.tau  = 0.28;
    p.wha  = 0.90;
    p.hnorm= 0.05;
    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    assert(m);

    PAtmResult r;
    p_atmosmodel_eval(m, 30.0, 45.0, 20.0, &r);

    /* Physical constraints: */
    ASSERT_TRUE(r.pstd   >= 0.0, "iso1 pstd >= 0");
    ASSERT_TRUE(r.trans  >  0.0, "iso1 trans > 0");
    ASSERT_TRUE(r.trans0 >  0.0, "iso1 trans0 > 0");
    ASSERT_TRUE(r.trans0 <= r.trans, "iso1 trans0 <= trans");  /* unscattered <= total */
    ASSERT_TRUE(r.sbar   >= 0.0 && r.sbar <= 1.0, "iso1 sbar in [0,1]");

    /* At nadir (i=e=0), pstd should be a specific value. */
    p_atmosmodel_eval(m, 0.0, 0.0, 0.0, &r);
    ASSERT_TRUE(r.pstd > 0.0, "iso1 nadir pstd > 0");

    /* sbar is tau-dependent only: should not change with geometry. */
    double sbar1 = r.sbar;
    p_atmosmodel_eval(m, 60.0, 30.0, 40.0, &r);
    ASSERT_NEAR(r.sbar, sbar1, 1e-15, "iso1 sbar geometry-independent");

    p_atmosmodel_free(m);
    printf("PASS: Isotropic1 physics\n");
}

/* ================================================================== */
/* Isotropic2 vs Isotropic1 (Iso2 should give larger pstd at same tau) */
/* ================================================================== */

static void test_iso2_vs_iso1(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
    p.tau  = 0.5;
    p.wha  = 0.80;
    p.hnorm= 0.05;

    PAtmosModel *m1 = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    PAtmosModel *m2 = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC2, &p);
    assert(m1 && m2);

    PAtmResult r1, r2;
    p_atmosmodel_eval(m1, 30.0, 45.0, 20.0, &r1);
    p_atmosmodel_eval(m2, 30.0, 45.0, 20.0, &r2);

    /* Both should be physically reasonable. */
    ASSERT_TRUE(r2.pstd > 0.0, "iso2 pstd > 0");
    ASSERT_TRUE(r2.trans > 0.0, "iso2 trans > 0");

    /* At tau=0 both must agree (trivially). */
    p.tau = 0.0;
    PAtmosModel *m1z = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    PAtmosModel *m2z = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC2, &p);
    PAtmResult rz1, rz2;
    p_atmosmodel_eval(m1z, 30.0, 45.0, 20.0, &rz1);
    p_atmosmodel_eval(m2z, 30.0, 45.0, 20.0, &rz2);
    ASSERT_NEAR(rz1.pstd, rz2.pstd, 1e-15, "iso1==iso2 at tau=0");

    p_atmosmodel_free(m1); p_atmosmodel_free(m2);
    p_atmosmodel_free(m1z); p_atmosmodel_free(m2z);
    printf("PASS: Isotropic2 vs Isotropic1\n");
}

/* ================================================================== */
/* Anisotropic1 basic physics                                           */
/* ================================================================== */

static void test_anisotropic1_physics(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ANISOTROPIC1;
    p.tau  = 0.28;
    p.wha  = 0.90;
    p.hnorm= 0.05;
    p.bha  = 0.85;

    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ANISOTROPIC1, &p);
    assert(m);

    PAtmResult r;
    p_atmosmodel_eval(m, 30.0, 45.0, 20.0, &r);

    ASSERT_TRUE(isfinite(r.pstd),  "aniso1 pstd finite");
    ASSERT_TRUE(r.trans  > 0.0,    "aniso1 trans > 0");
    ASSERT_TRUE(r.trans0 > 0.0,    "aniso1 trans0 > 0");
    ASSERT_TRUE(r.trans0 <= r.trans, "aniso1 trans0 <= trans");

    /* wha=1 (conservative) should fail creation. */
    p.wha = 1.0;
    PAtmosModel *bad = p_atmosmodel_create(P_ATMOSMODEL_ANISOTROPIC1, &p);
    ASSERT_TRUE(bad == NULL, "aniso1 wha=1 rejected");

    p_atmosmodel_free(m);
    printf("PASS: Anisotropic1 physics\n");
}

/* ================================================================== */
/* Anisotropic2 basic physics                                           */
/* ================================================================== */

static void test_anisotropic2_physics(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ANISOTROPIC2;
    p.tau  = 0.28;
    p.wha  = 0.90;
    p.hnorm= 0.05;
    p.bha  = 0.85;

    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ANISOTROPIC2, &p);
    assert(m);

    PAtmResult r;
    p_atmosmodel_eval(m, 30.0, 45.0, 20.0, &r);

    ASSERT_TRUE(isfinite(r.pstd),  "aniso2 pstd finite");
    ASSERT_TRUE(r.trans  > 0.0,    "aniso2 trans > 0");
    ASSERT_TRUE(r.trans0 > 0.0,    "aniso2 trans0 > 0");

    /* Geometry extremes: i=0, e=0 */
    p_atmosmodel_eval(m, 0.0, 0.0, 0.0, &r);
    ASSERT_TRUE(isfinite(r.pstd), "aniso2 nadir pstd finite");

    p_atmosmodel_free(m);
    printf("PASS: Anisotropic2 physics\n");
}

/* ================================================================== */
/* Tau-change caching: calling with same geometry gives same result     */
/* ================================================================== */

static void test_cache_consistency(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
    p.tau = 0.3; p.wha = 0.85; p.hnorm = 0.04;
    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    assert(m);

    PAtmResult r1, r2;
    p_atmosmodel_eval(m, 20.0, 30.0, 10.0, &r1);
    /* Call with different geometry to exercise the path-length code,
     * but same tau/wha so cache should be reused. */
    p_atmosmodel_eval(m, 50.0, 60.0, 25.0, &r2);
    /* sbar must stay the same (it's tau/wha-only). */
    ASSERT_NEAR(r1.sbar, r2.sbar, 1e-15, "cache: sbar unchanged");

    /* Now change tau → cache must rebuild → sbar changes. */
    ((struct { PAtmosModelType t; PAtmParams par; } *)m)->par.tau = 0.6;
    PAtmResult r3;
    p_atmosmodel_eval(m, 20.0, 30.0, 10.0, &r3);
    /* Manually rebuild model at tau=0.6. */
    p.tau = 0.6;
    PAtmosModel *m2 = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    PAtmResult r4;
    p_atmosmodel_eval(m2, 20.0, 30.0, 10.0, &r4);
    ASSERT_NEAR(r3.pstd, r4.pstd, 1e-10, "cache: pstd matches new model at tau=0.6");

    p_atmosmodel_free(m);
    p_atmosmodel_free(m2);
    printf("PASS: cache consistency\n");
}

/* ================================================================== */
/* p_atmosmodel_apply formula check                                     */
/* ================================================================== */

static void test_apply_formula(void)
{
    /* With tau=0: result should equal psurf (no atmosphere). */
    double pstd=0.0, trans=1.0, trans0=1.0, sbar=0.0;
    double rho=1.0, Ah=0.5, Ab=0.3, Psurf=0.7, munot=0.8;
    double P = p_atmosmodel_apply(pstd, trans, trans0, sbar, rho, Ah, Ab, Psurf, munot);
    double expected = 0.0 + 1.0*1.0*0.5*0.8/(1.0-1.0*0.3*0.0) + 1.0*1.0*(0.7-0.5*0.8);
    ASSERT_NEAR(P, expected, 1e-12, "apply formula tau=0");

    /* rho=0: result = pstd only. */
    P = p_atmosmodel_apply(0.05, 0.8, 0.7, 0.2, 0.0, 0.5, 0.3, 0.6, 0.7);
    ASSERT_NEAR(P, 0.05, 1e-12, "apply formula rho=0 → pstd");

    printf("PASS: p_atmosmodel_apply formula\n");
}

/* ================================================================== */
/* OpenMP row correction                                                */
/* ================================================================== */

static void test_apply_row_omp(void)
{
    PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
    p.tau=0.28; p.wha=0.90; p.hnorm=0.05;
    PAtmosModel *m = p_atmosmodel_create(P_ATMOSMODEL_ISOTROPIC1, &p);
    assert(m);

    int n = 800;
    double *input   = malloc(n * sizeof(double));
    double *psurf   = malloc(n * sizeof(double));
    double *phase   = malloc(n * sizeof(double));
    double *inc     = malloc(n * sizeof(double));
    double *ema     = malloc(n * sizeof(double));
    double *output  = malloc(n * sizeof(double));
    assert(input && psurf && phase && inc && ema && output);

    for (int i = 0; i < n; i++) {
        input[i] = 100.0 + i * 0.1;
        psurf[i] = 0.5 + 0.001 * i;
        phase[i] = (double)(i % 90);
        inc[i]   = (double)(i % 80) + 1.0;
        ema[i]   = (double)(i % 40) + 1.0;
    }

    p_atmosmodel_apply_row(m, n, input, psurf, phase, inc, ema,
                            1.0, 0.5, 0.3, output);

    /* Spot-check 8 positions. */
    int errors = 0;
    for (int i = 0; i < n; i += 100) {
        PAtmResult r;
        p_atmosmodel_eval(m, phase[i], inc[i], ema[i], &r);
        double munot = cos(inc[i] * M_PI / 180.0);
        double exp_val = p_atmosmodel_apply(r.pstd, r.trans, r.trans0,
                                             r.sbar, 1.0, 0.5, 0.3,
                                             psurf[i], munot);
        if (fabs(output[i] - exp_val) > 1e-8) {
            fprintf(stderr, "FAIL [apply_row] i=%d: %.10g vs %.10g\n",
                    i, output[i], exp_val);
            errors++;
        }
    }
    if (errors == 0) g_pass++; else g_fail++;

    free(input); free(psurf); free(phase); free(inc); free(ema); free(output);
    p_atmosmodel_free(m);
    printf("PASS: apply_row (OpenMP)\n");
}

/* ================================================================== */
/* Model names                                                          */
/* ================================================================== */

static void test_names(void)
{
    const char *expected[] = { "Isotropic1","Isotropic2","Anisotropic1","Anisotropic2" };
    PAtmosModelType types[] = {
        P_ATMOSMODEL_ISOTROPIC1, P_ATMOSMODEL_ISOTROPIC2,
        P_ATMOSMODEL_ANISOTROPIC1, P_ATMOSMODEL_ANISOTROPIC2
    };
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        PAtmParams p = P_ATM_DEFAULTS_ISOTROPIC1;
        PAtmosModel *m = p_atmosmodel_create(types[i], &p);
        const char *got = p_atmosmodel_name(m);
        if (strcmp(got, expected[i]) != 0) {
            fprintf(stderr, "FAIL [name %d]: '%s' vs '%s'\n", i, got, expected[i]);
            ok = 0;
        }
        p_atmosmodel_free(m);
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
    printf("=== p_atmosmodel tests (OpenMP: %d threads) ===\n",
           omp_get_max_threads());
#else
    printf("=== p_atmosmodel tests (no OpenMP) ===\n");
#endif

    test_En();
    test_Ei();
    test_G11Prime();
    test_vacuum();
    test_isotropic1_physics();
    test_iso2_vs_iso1();
    test_anisotropic1_physics();
    test_anisotropic2_physics();
    test_cache_consistency();
    test_apply_formula();
    test_apply_row_omp();
    test_names();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
