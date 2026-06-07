/*!
 * \file test_p_spectra.c
 *
 * \brief Unit tests for p_spectra library.
 *
 * Compile:
 *   gcc -std=c99 -DP_SPECTRA_STANDALONE -fopenmp \
 *       -o test_p_spectra test_p_spectra.c p_spectra.c -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "p_spectra.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _OPENMP
#  include <omp.h>
#endif
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* Standalone stubs - needed by p_spectra.c via -DP_SPECTRA_STANDALONE */
static int g_pass=0, g_fail=0;

#define ASSERT_NEAR(a,b,eps,msg) \
    do{ double _a=(a),_b=(b),_e=(eps); \
        if(fabs(_a-_b)<=_e){g_pass++;} \
        else{fprintf(stderr,"FAIL [%s]: %.12g vs %.12g (tol %.2g)\n",(msg),_a,_b,_e);g_fail++;} \
    }while(0)
#define ASSERT_TRUE(c,msg) \
    do{if(c){g_pass++;}else{fprintf(stderr,"FAIL [%s]\n",(msg));g_fail++;}}while(0)
#define ASSERT_NAN(a,msg) \
    do{ double _a=(a); \
        if(_a!=_a){g_pass++;} \
        else{fprintf(stderr,"FAIL [%s]: expected NaN, got %.12g\n",(msg),_a);g_fail++;} \
    }while(0)

/* ================================================================== */
/* Build a simple 5-band VNIR definition: 0.5, 0.7, 0.9, 1.1, 1.3 µm */
/* ================================================================== */
static PSpectraDef *make_vnir5(void)
{
    double wl[] = { 0.5, 0.7, 0.9, 1.1, 1.3 };
    double wd[] = { 0.1, 0.1, 0.1, 0.1, 0.1 };
    return p_spectra_def_create(5, wl, wd);
}

/* ================================================================== */
/* Spectral definition tests                                            */
/* ================================================================== */

static void test_def_create(void)
{
    PSpectraDef *sd = make_vnir5();
    assert(sd);

    ASSERT_TRUE(p_spectra_nbands(sd) == 5,     "def nbands=5");
    ASSERT_NEAR(p_spectra_wavelength(sd, 0), 0.5, 1e-12, "def wl[0]");
    ASSERT_NEAR(p_spectra_wavelength(sd, 4), 1.3, 1e-12, "def wl[4]");
    ASSERT_NEAR(p_spectra_width(sd, 2),      0.1, 1e-12, "def width[2]");
    /* Out-of-range: NaN */
    ASSERT_NAN(p_spectra_wavelength(sd, 5),  "def wl[5] NaN");
    ASSERT_NAN(p_spectra_width(sd, -1),      "def width[-1] NaN");

    /* Single section (monotone ascending). */
    ASSERT_TRUE(p_spectra_section_count(sd) == 1, "def 1 section");

    p_spectra_def_free(sd);
    printf("PASS: def create\n");
}

static void test_def_multisection(void)
{
    /* Wavelengths that go up then down → 2 sections. */
    double wl[] = { 1.0, 1.5, 2.0, 1.8, 1.2 };
    double wd[] = { 0.1, 0.1, 0.1, 0.1, 0.1 };
    PSpectraDef *sd = p_spectra_def_create(5, wl, wd);
    assert(sd);

    ASSERT_TRUE(p_spectra_section_count(sd) == 2, "multisec: 2 sections");

    /* Band 0–2 → section 0, bands 3–4 → section 1. */
    ASSERT_TRUE(p_spectra_section_of_band(sd, 0) == 0, "multisec band 0 → sec 0");
    ASSERT_TRUE(p_spectra_section_of_band(sd, 2) == 0, "multisec band 2 → sec 0");
    ASSERT_TRUE(p_spectra_section_of_band(sd, 3) == 1, "multisec band 3 → sec 1");

    p_spectra_def_free(sd);
    printf("PASS: def multi-section detection\n");
}

static void test_find_band(void)
{
    PSpectraDef *sd = make_vnir5();
    assert(sd);

    /* Exact matches */
    ASSERT_TRUE(p_spectra_find_band(sd, 0.5, 0) == 0, "find_band 0.5 → 0");
    ASSERT_TRUE(p_spectra_find_band(sd, 0.9, 0) == 2, "find_band 0.9 → 2");
    ASSERT_TRUE(p_spectra_find_band(sd, 1.3, 0) == 4, "find_band 1.3 → 4");

    /* Nearest-neighbour */
    ASSERT_TRUE(p_spectra_find_band(sd, 0.62, 0) == 1, "find_band 0.62 → 1(0.7)");
    ASSERT_TRUE(p_spectra_find_band(sd, 0.78, 0) == 1, "find_band 0.78 → 1(0.7)");
    ASSERT_TRUE(p_spectra_find_band(sd, 0.81, 0) == 2, "find_band 0.81 → 2(0.9)");

    p_spectra_def_free(sd);
    printf("PASS: find_band nearest-neighbour\n");
}

static void test_def_read_csv(void)
{
    /* Write a tiny CSV to /tmp and read it back. */
    const char *path = "/tmp/test_p_spectra_wl.csv";
    FILE *fp = fopen(path, "w");
    assert(fp);
    fprintf(fp, "# wavelength, width\n");
    fprintf(fp, "0.5,0.1\n");
    fprintf(fp, "0.7,0.1\n");
    fprintf(fp, "0.9,0.1\n");
    fclose(fp);

    PSpectraDef *sd = p_spectra_def_read_csv(path);
    assert(sd);
    ASSERT_TRUE(p_spectra_nbands(sd) == 3,      "csv nbands=3");
    ASSERT_NEAR(p_spectra_wavelength(sd, 0), 0.5, 1e-12, "csv wl[0]");
    ASSERT_NEAR(p_spectra_wavelength(sd, 2), 0.9, 1e-12, "csv wl[2]");

    p_spectra_def_free(sd);
    remove(path);
    printf("PASS: def read CSV\n");
}

/* ================================================================== */
/* Band depth tests                                                     */
/* ================================================================== */

static void test_band_depth(void)
{
    PSpectraDef *sd = make_vnir5();
    assert(sd);

    /*
     * Spectrum: flat at 1.0 except band 2 (0.9 µm) dips to 0.6.
     * Continuum from band 0 (0.5 µm, R=1.0) to band 4 (1.3 µm, R=1.0).
     * At wl=0.9: Rc = 1.0, R = 0.6 → BD = 1 - 0.6/1.0 = 0.4
     */
    double spec[] = { 1.0, 1.0, 0.6, 1.0, 1.0 };
    double bd = p_spectra_band_depth(sd, spec, 0.9, 0.5, 1.3, 0);
    ASSERT_NEAR(bd, 0.4, 1e-10, "band_depth 0.4");

    /* Flat spectrum → BD = 0 */
    double flat[] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
    bd = p_spectra_band_depth(sd, flat, 0.9, 0.5, 1.3, 0);
    ASSERT_NEAR(bd, 0.0, 1e-10, "band_depth flat=0");

    /* Sloped continuum: R increases from 0.5 to 1.5 linearly.
     * Band values: 0.5, 0.75, 1.0, 1.25, 1.5 (on the continuum → BD=0). */
    double slope[] = { 0.5, 0.75, 1.0, 1.25, 1.5 };
    bd = p_spectra_band_depth(sd, slope, 0.9, 0.5, 1.3, 0);
    ASSERT_NEAR(bd, 0.0, 1e-10, "band_depth sloped continuum=0");

    /* Center ON left anchor → BD depends only on band value vs left R. */
    bd = p_spectra_band_depth(sd, spec, 0.5, 0.5, 1.3, 0);
    ASSERT_NEAR(bd, 0.0, 1e-10, "band_depth at left anchor");

    p_spectra_def_free(sd);
    printf("PASS: band depth\n");
}

/* ================================================================== */
/* Band ratio tests                                                     */
/* ================================================================== */

static void test_band_ratio(void)
{
    PSpectraDef *sd = make_vnir5();
    double spec[] = { 0.2, 0.4, 0.6, 0.8, 1.0 };

    /* R(0.5)/R(1.3) = 0.2/1.0 = 0.2 */
    ASSERT_NEAR(p_spectra_band_ratio(sd, spec, 0.5, 1.3, 0), 0.2, 1e-10, "ratio 0.2");

    /* R(1.3)/R(0.5) = 1.0/0.2 = 5.0 */
    ASSERT_NEAR(p_spectra_band_ratio(sd, spec, 1.3, 0.5, 0), 5.0, 1e-10, "ratio 5.0");

    /* Zero denominator → NaN */
    double z[] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    ASSERT_NAN(p_spectra_band_ratio(sd, z, 0.5, 1.3, 0), "ratio denom=0 → NaN");

    p_spectra_def_free(sd);
    printf("PASS: band ratio\n");
}

/* ================================================================== */
/* Spectral Angle Mapper tests                                          */
/* ================================================================== */

static void test_sam(void)
{
    int nb = 5;

    /* Identical spectra → angle ≈ 0 (acos(1.0) has ~2e-8 float uncertainty). */
    double s1[] = { 1.0, 2.0, 3.0, 2.0, 1.0 };
    ASSERT_NEAR(p_spectra_sam(nb, s1, s1), 0.0, 1e-6, "SAM identical=0");

    /* Anti-parallel (one negated) → angle ≈ π */
    double s2[] = { -1.0, -2.0, -3.0, -2.0, -1.0 };
    ASSERT_NEAR(p_spectra_sam(nb, s1, s2), M_PI, 1e-6, "SAM antiparallel=π");

    /* Orthogonal 2-band: (1,0) vs (0,1) → angle = π/2 */
    double a[] = { 1.0, 0.0 };
    double b[] = { 0.0, 1.0 };
    ASSERT_NEAR(p_spectra_sam(2, a, b), M_PI/2.0, 1e-10, "SAM orthogonal=π/2");

    /* All-zero reference → NaN */
    double z[] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    ASSERT_NAN(p_spectra_sam(nb, s1, z), "SAM zero ref → NaN");

    printf("PASS: spectral angle mapper\n");
}

/* ================================================================== */
/* Continuum removal tests                                              */
/* ================================================================== */

static void test_continuum_remove(void)
{
    PSpectraDef *sd = make_vnir5();
    assert(sd);

    /* Flat spectrum: after removal all bands should be 1.0. */
    double flat[] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
    double out[5];
    p_spectra_continuum_remove(sd, flat, 5, 0.5, 1.3, 0, out);
    for (int b = 0; b < 5; b++)
        ASSERT_NEAR(out[b], 1.0, 1e-10, "cont_remove flat→1");

    /* Sloped continuum exactly on the line → all 1.0 after removal. */
    double slope[] = { 0.5, 0.75, 1.0, 1.25, 1.5 };
    p_spectra_continuum_remove(sd, slope, 5, 0.5, 1.3, 0, out);
    for (int b = 0; b < 5; b++)
        ASSERT_NEAR(out[b], 1.0, 1e-8, "cont_remove sloped→1");

    /* Band outside range is passed through unchanged. */
    p_spectra_continuum_remove(sd, flat, 5, 0.7, 1.1, 0, out);
    ASSERT_NEAR(out[0], 1.0, 1e-12, "cont_remove out-of-range band 0 passthrough");
    ASSERT_NEAR(out[4], 1.0, 1e-12, "cont_remove out-of-range band 4 passthrough");

    p_spectra_def_free(sd);
    printf("PASS: continuum removal\n");
}

/* ================================================================== */
/* Spectral highpass / divfilter tests                                  */
/* ================================================================== */

static void test_highpass(void)
{
    /* Flat spectrum: highpass → all zeros. */
    double flat[] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
    double out[5];
    p_spectra_highpass(flat, 5, 3, out);
    for (int b = 0; b < 5; b++)
        ASSERT_NEAR(out[b], 0.0, 1e-10, "highpass flat→0");

    /* Linear ramp: running mean of a ramp equals the central value
     * so highpass should be 0 for interior points. */
    double ramp[] = { 0.0, 1.0, 2.0, 3.0, 4.0 };
    p_spectra_highpass(ramp, 5, 3, out);
    /* Interior (b=1,2,3): mean of symmetric window = ramp[b] → 0 */
    ASSERT_NEAR(out[2], 0.0, 1e-10, "highpass ramp interior=0");

    /* NaN input propagates */
    double nan_spec[] = { 1.0, 1.0, NAN, 1.0, 1.0 };
    p_spectra_highpass(nan_spec, 5, 3, out);
    /* NaN band → NaN output */
    ASSERT_NAN(out[2], "highpass NaN band → NaN out");
    /* Neighbours: window includes NaN but others are valid; mean skips NaN */
    ASSERT_TRUE(out[1] == out[1], "highpass NaN neighbour: band 1 still finite");

    printf("PASS: spectral highpass\n");
}

static void test_divfilter(void)
{
    /* Flat spectrum: divfilter → all 1.0. */
    double flat[] = { 2.0, 2.0, 2.0, 2.0, 2.0 };
    double out[5];
    p_spectra_divfilter(flat, 5, 3, out);
    for (int b = 0; b < 5; b++)
        ASSERT_NEAR(out[b], 1.0, 1e-10, "divfilter flat→1");

    /* Band with value twice the running mean → 2.0 */
    double spike[] = { 1.0, 1.0, 2.0, 1.0, 1.0 };
    p_spectra_divfilter(spike, 5, 5, out);
    /* Mean of full 5-band window = (1+1+2+1+1)/5 = 1.2; out[2] = 2/1.2 */
    ASSERT_NEAR(out[2], 2.0/1.2, 1e-10, "divfilter spike/mean");

    /* Zero spectrum → NaN */
    double z[] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    p_spectra_divfilter(z, 5, 3, out);
    for (int b = 0; b < 5; b++)
        ASSERT_NAN(out[b], "divfilter zero→NaN");

    printf("PASS: spectral divfilter\n");
}

/* ================================================================== */
/* Row-level parallel operations                                        */
/* ================================================================== */

static void test_apply_row_omp(void)
{
    PSpectraDef *sd = make_vnir5();
    assert(sd);

    int ns = 500, nb = 5;
    double *spectra = (double *)malloc((size_t)ns * nb * sizeof(double));
    double *out     = (double *)malloc((size_t)ns * sizeof(double));
    assert(spectra && out);

    /* All spectra: flat=1.0 except band 2 dips to 0.6 (BD should be 0.4). */
    for (int s = 0; s < ns; s++)
        for (int b = 0; b < nb; b++)
            spectra[s*nb + b] = (b == 2) ? 0.6 : 1.0;

    p_spectra_apply_row_band_depth(sd, ns, nb, spectra, 0.9, 0.5, 1.3, 0, out);
    for (int s = 0; s < ns; s++)
        ASSERT_NEAR(out[s], 0.4, 1e-10, "apply_row_band_depth");

    /* Band ratio: R(0.5)/R(1.3) = 1.0/1.0 = 1.0 */
    p_spectra_apply_row_band_ratio(sd, ns, nb, spectra, 0.5, 1.3, 0, out);
    for (int s = 0; s < ns; s++)
        ASSERT_NEAR(out[s], 1.0, 1e-10, "apply_row_band_ratio");

    /* SAM: each spectrum vs itself → 0 */
    /* Use the first spectrum as reference. */
    p_spectra_apply_row_sam(ns, nb, spectra, spectra + 0, out);
    for (int s = 0; s < ns; s++)
        ASSERT_NEAR(out[s], 0.0, 1e-8, "apply_row_sam identical");

    /* Highpass: flat spectrum minus running mean = 0. */
    double *out2d = (double *)malloc((size_t)ns * nb * sizeof(double));
    assert(out2d);
    double *flat_row = (double *)malloc((size_t)ns * nb * sizeof(double));
    assert(flat_row);
    for (int s = 0; s < ns; s++)
        for (int b = 0; b < nb; b++)
            flat_row[s*nb+b] = 1.0;

    p_spectra_apply_row_highpass(ns, nb, flat_row, 3, out2d);
    for (int s = 0; s < ns; s++)
        for (int b = 0; b < nb; b++)
            ASSERT_NEAR(out2d[s*nb+b], 0.0, 1e-10, "apply_row_highpass flat");

    /* Divfilter: flat → 1.0 */
    p_spectra_apply_row_divfilter(ns, nb, flat_row, 3, out2d);
    for (int s = 0; s < ns; s++)
        for (int b = 0; b < nb; b++)
            ASSERT_NEAR(out2d[s*nb+b], 1.0, 1e-10, "apply_row_divfilter flat");

    free(spectra); free(out); free(out2d); free(flat_row);
    p_spectra_def_free(sd);
    printf("PASS: apply_row operations (OpenMP, %d samples)\n", ns);
}

/* ================================================================== */
/* ISIS3 reference: spechighpass behaviour                              */
/* A spectrum that rises then falls; interior window gives zero for    */
/* symmetric data.                                                      */
/* ================================================================== */

static void test_isis3_spechighpass_reference(void)
{
    /* Symmetric ramp-peak: [0,1,2,3,2,1,0] — window=3
     * QuickFilter average at b=3: (3+2+3+2)/4? No — simple mean of [2,3,2] = 7/3
     * ISIS3: output[b] = in[b] - average(b, window)
     *   b=3: mean([2,3,2]) = 7/3; out = 3 - 7/3 = 2/3
     *   b=1: mean([0,1,2]) = 1; out = 1-1 = 0
     * Wait — actually with window=3, half=1 so range is [b-1, b+1].
     * b=1: [0,1,2] → mean=1 → out=1-1=0 ✓
     * b=3: [2,3,2] → mean=7/3 → out=3-7/3=2/3 ✓
     */
    double spec[] = { 0.0, 1.0, 2.0, 3.0, 2.0, 1.0, 0.0 };
    double out[7];
    p_spectra_highpass(spec, 7, 3, out);

    ASSERT_NEAR(out[1], 0.0,     1e-10, "spechighpass ref b=1");
    ASSERT_NEAR(out[3], 2.0/3.0, 1e-10, "spechighpass ref b=3");
    /* b=5: [2,1,0] → mean=1 → out=1-1=0 */
    ASSERT_NEAR(out[5], 0.0,     1e-10, "spechighpass ref b=5");

    printf("PASS: ISIS3 spechighpass reference\n");
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void)
{
#ifdef _OPENMP
    printf("=== p_spectra tests (OpenMP: %d threads) ===\n",
           omp_get_max_threads());
#else
    printf("=== p_spectra tests (no OpenMP) ===\n");
#endif

    test_def_create();
    test_def_multisection();
    test_find_band();
    test_def_read_csv();
    test_band_depth();
    test_band_ratio();
    test_sam();
    test_continuum_remove();
    test_highpass();
    test_divfilter();
    test_apply_row_omp();
    test_isis3_spechighpass_reference();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
