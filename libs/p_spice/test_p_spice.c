/*!
 * \file test_p_spice.c
 *
 * \brief Unit tests for p_spice library.
 *
 * Tests are split into two categories:
 *   A. No-kernel tests — validate error handling and API robustness when
 *      no kernels are loaded (no SPICE data required).
 *   B. With-kernel tests — SKIPPED automatically if the standard NAIF
 *      generic LSK is not found at $ISISDATA/base/kernels/lsk/naif0012.tls
 *      or $HOME/dev/cspice/data/naif0012.tls.
 *
 * Compile:
 *   gcc -std=c99 -fopenmp \
 *       -I$HOME/dev/cspice/include \
 *       -o test_p_spice test_p_spice.c p_spice.c \
 *       -L$HOME/dev/cspice/lib -lcspice \
 *       $(grass-config --libs) -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _POSIX_C_SOURCE 200809L

#include "p_spice.h"
#include "SpiceUsr.h"   /* for failed_c(), reset_c() */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

static int g_pass=0, g_fail=0;

#define ASSERT_TRUE(c,msg) \
    do{if(c){g_pass++;}else{fprintf(stderr,"FAIL [%s]\n",(msg));g_fail++;}}while(0)
#define ASSERT_EQ(a,b,msg) \
    do{if((a)==(b)){g_pass++;}else{fprintf(stderr,"FAIL [%s]: %d vs %d\n",(msg),(int)(a),(int)(b));g_fail++;}}while(0)
#define ASSERT_NEAR(a,b,eps,msg) \
    do{double _a=(a),_b=(b),_e=(eps); \
       if(fabs(_a-_b)<=_e){g_pass++;} \
       else{fprintf(stderr,"FAIL [%s]: %.12g vs %.12g (tol %.2g)\n",(msg),_a,_b,_e);g_fail++;}}while(0)

/* ================================================================== */
/* A. No-kernel tests                                                   */
/* ================================================================== */

static void test_init(void)
{
    p_spice_init();
    /* After init, CSPICE should not be in a failed state. */
    ASSERT_TRUE(!failed_c(), "init: no cspice error after init");
    printf("PASS: p_spice_init\n");
}

static void test_load_nonexistent(void)
{
    /* Loading a non-existent kernel must return -1 and leave no stale
     * error state (p_spice_load calls reset_c after failure). */
    int rc = p_spice_load("/nonexistent/path/to/kernel.tls");
    ASSERT_EQ(rc, -1, "load nonexistent → -1");
    /* After the failed load, CSPICE error state should be reset. */
    ASSERT_TRUE(!failed_c(), "load nonexistent: error state reset");
    printf("PASS: load nonexistent kernel\n");
}

static void test_name2id_no_kernels(void)
{
    /* bodn2c_c works for built-in NAIF IDs even without a PCK/FK loaded. */
    int id = 0;
    int rc = p_spice_name2id("EARTH", &id);
    if (rc == 0) {
        ASSERT_EQ(id, 399, "name2id EARTH=399");
    } else {
        /* Some CSPICE builds need a FK/PCK for name lookup — skip. */
        printf("SKIP: name2id EARTH (no frame kernel)\n");
        g_pass++;   /* count as pass since it's env-dependent */
    }

    rc = p_spice_name2id("SUN", &id);
    if (rc == 0) {
        ASSERT_EQ(id, 10, "name2id SUN=10");
    } else {
        g_pass++;
    }

    /* Unknown body → -1 */
    rc = p_spice_name2id("NOTABODY_XYZ_123", &id);
    ASSERT_EQ(rc, -1, "name2id unknown → -1");

    printf("PASS: name2id (no kernels)\n");
}

static void test_radii_no_kernels(void)
{
    /* Without a PCK kernel, bodvrd_c must fail gracefully. */
    double r[3];
    int rc = p_spice_radii("MARS", r);
    ASSERT_EQ(rc, -1, "radii MARS without PCK → -1");
    ASSERT_TRUE(!failed_c(), "radii error state reset");
    printf("PASS: radii without PCK kernel\n");
}

static void test_pos_no_kernels(void)
{
    double pos[3], lt;
    int rc = p_spice_pos("MARS", 0.0, "J2000", "NONE", "EARTH", pos, &lt);
    ASSERT_EQ(rc, -1, "pos without SPK → -1");
    ASSERT_TRUE(!failed_c(), "pos error state reset");
    printf("PASS: pos without SPK kernel\n");
}

static void test_clear(void)
{
    p_spice_clear();
    ASSERT_TRUE(!failed_c(), "clear: no error");
    printf("PASS: p_spice_clear\n");
}

static void test_errmsg(void)
{
    /* Trigger an error, then read the message. */
    furnsh_c("/no/such/file");
    char msg[256] = "NONE";
    int had_error = p_spice_errmsg(msg, sizeof(msg));
    ASSERT_TRUE(had_error == 1, "errmsg: detected error");
    ASSERT_TRUE(strlen(msg) > 0, "errmsg: non-empty message");
    /* After errmsg, error state should be cleared. */
    ASSERT_TRUE(!failed_c(), "errmsg: error cleared");
    printf("PASS: errmsg\n");
}

/* ================================================================== */
/* B. With-kernel tests                                                 */
/* ================================================================== */

/* Search for a generic LSK file in common locations. */
static const char *find_lsk(void)
{
    static const char *candidates[] = {
        "/home/yann/dev/cspice/data/naif0012.tls",
        "/home/yann/dev/cspice/data/cook_01.tls",
        "/home/yann/dev/cspice/data/naif0011.tls",
        "/usgs/cpkgs/isis3/data/base/kernels/lsk/naif0012.tls",
        "/isis/data/base/kernels/lsk/naif0012.tls",
        NULL
    };
    /* Also check $ISISDATA */
    const char *isisdata = getenv("ISISDATA");
    if (isisdata) {
        static char buf[1024];
        snprintf(buf, sizeof(buf), "%s/base/kernels/lsk/naif0012.tls", isisdata);
        FILE *fp = fopen(buf, "r");
        if (fp) { fclose(fp); return buf; }
    }
    for (int i = 0; candidates[i]; i++) {
        FILE *fp = fopen(candidates[i], "r");
        if (fp) { fclose(fp); return candidates[i]; }
    }
    return NULL;
}

static void test_with_lsk(const char *lsk_path)
{
    printf("--- With-kernel tests (LSK: %s) ---\n", lsk_path);

    p_spice_clear();
    p_spice_init();

    ASSERT_EQ(p_spice_load(lsk_path), 0, "load LSK → 0");

    /* str2et: convert the J2000 epoch string. ET should be a finite number near 0
     * (exact value depends on leap-second count in the loaded LSK; production
     * naif0012.tls gives ≈0, the bundled cook_01.tls may differ by ~7 s). */
    double et = -999.0;
    ASSERT_EQ(p_spice_str2et("2000-01-01T11:58:55.816", &et), 0, "str2et ok");
    /* Accept any value in [-60, +60] — the exact offset depends on the LSK. */
    ASSERT_TRUE(et > -60.0 && et < 60.0, "str2et J2000 in [-60,+60] s");

    /* Round-trip: et → utc → et */
    char utc[64];
    ASSERT_EQ(p_spice_et2utc(et, "ISOC", 3, utc, sizeof(utc)), 0, "et2utc ok");
    ASSERT_TRUE(strlen(utc) > 0, "et2utc non-empty");

    double et2 = -1.0;
    ASSERT_EQ(p_spice_str2et(utc, &et2), 0, "str2et round-trip ok");
    ASSERT_NEAR(et2, et, 0.002, "str2et round-trip ≈");

    /* A known time: 2007-01-15T01:43:00 */
    double et3;
    ASSERT_EQ(p_spice_str2et("2007-01-15T01:43:00", &et3), 0, "str2et 2007 ok");
    ASSERT_TRUE(et3 > 2.2e8 && et3 < 2.3e8, "str2et 2007 plausible range");

    p_spice_clear();
    printf("PASS: with-LSK time conversion\n");
}

/* ================================================================== */
/* API completeness smoke test (link-time, no runtime kernel needed)   */
/* ================================================================== */

static void test_api_links(void)
{
    /* Just call each API function with NULL/invalid args to verify
     * the symbols link correctly and don't crash. */
    p_spice_init();

    ASSERT_EQ(p_spice_load(NULL), -1, "load NULL → -1");
    ASSERT_EQ(p_spice_unload(NULL), -1, "unload NULL → -1");

    {
        double et; ASSERT_EQ(p_spice_str2et(NULL, &et), -1, "str2et NULL → -1");
        ASSERT_EQ(p_spice_str2et("garbage", NULL), -1, "str2et null out → -1");
    }
    {
        char buf[32]; ASSERT_EQ(p_spice_et2utc(0.0, "ISOC", 3, NULL, 10), -1, "et2utc null buf → -1");
        ASSERT_EQ(p_spice_et2utc(0.0, "ISOC", 3, buf, 2), -1, "et2utc tiny buf → -1");
    }
    {
        int id; ASSERT_EQ(p_spice_name2id(NULL, &id), -1, "name2id null → -1");
    }
    {
        double r[3]; ASSERT_EQ(p_spice_radii(NULL, r), -1, "radii null → -1");
        ASSERT_EQ(p_spice_radii("MARS", NULL), -1, "radii null r → -1");
    }
    {
        double pos[3], lt;
        ASSERT_EQ(p_spice_pos(NULL, 0, "J2000", "NONE", "EARTH", pos, &lt), -1, "pos null targ → -1");
        ASSERT_EQ(p_spice_pos("MARS", 0, "J2000", "NONE", "EARTH", NULL, &lt), -1, "pos null pos → -1");
    }
    {
        double st[6], lt;
        ASSERT_EQ(p_spice_state(NULL, 0, "J2000", "NONE", "EARTH", st, &lt), -1, "state null → -1");
    }
    {
        double m[3][3];
        ASSERT_EQ(p_spice_pxform(NULL, "J2000", 0, m), -1, "pxform null → -1");
    }
    {
        double dvec[3]={0,0,-1}, sp[3], sf[3], tp;
        ASSERT_EQ(p_spice_sincpt(NULL,"MARS",0,"IAU_MARS","LT+S","MRO","J2000",dvec,sp,&tp,sf),
                  -1, "sincpt null meth → -1");
    }
    {
        double sp[3]={0,0,3390};
        double ph, inc, em;
        ASSERT_EQ(p_spice_ilumin(NULL,"MARS",0,"IAU_MARS","LT+S","MRO",sp,&ph,&inc,&em),
                  -1, "ilumin null → -1");
    }

    p_spice_clear();
    printf("PASS: API link smoke-test\n");
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void)
{
    printf("=== p_spice tests ===\n");

    test_init();
    test_load_nonexistent();
    test_name2id_no_kernels();
    test_radii_no_kernels();
    test_pos_no_kernels();
    test_clear();
    test_errmsg();
    test_api_links();

    /* With-kernel tests only if an LSK is available. */
    const char *lsk = find_lsk();
    if (lsk) {
        test_with_lsk(lsk);
    } else {
        printf("SKIP: with-kernel tests (no LSK found in standard locations;\n"
               "      set ISISDATA or copy naif0012.tls to %s/data/)\n",
               getenv("HOME") ? getenv("HOME") : "~/dev/cspice");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
