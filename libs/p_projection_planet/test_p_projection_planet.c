/*!
 * \file test_p_projection_planet.c
 *
 * \brief Unit tests for p_projection_planet library.
 *
 * Compile:
 *   gcc -std=c99 -DP_PROJ_PLANET_STANDALONE -fopenmp \
 *       -o test_p_projection_planet test_p_projection_planet.c \
 *       p_projection_planet.c -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "p_projection_planet.h"
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
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

static int g_pass=0, g_fail=0;

#define ASSERT_NEAR(a,b,eps,msg) \
    do{ double _a=(a),_b=(b),_e=(eps); \
        if(fabs(_a-_b)<=_e){g_pass++;} \
        else{fprintf(stderr,"FAIL [%s]: %.12g vs %.12g (tol %.2g)\n",(msg),_a,_b,_e);g_fail++;} \
    }while(0)
#define ASSERT_TRUE(c,msg) \
    do{if(c){g_pass++;}else{fprintf(stderr,"FAIL [%s]\n",(msg));g_fail++;}}while(0)

/* ================================================================== */
/* RingCylindrical                                                      */
/* ================================================================== */

static void test_ring_cyl_origin(void)
{
    /* At (center_radius, center_lon), map coords must be (0,0). */
    PProjPlanetParams par = P_PROJ_RING_CYL_DEFAULTS;
    par.ring_cyl.center_radius  = 100000.0;
    par.ring_cyl.center_lon_deg = 90.0;
    par.ring_cyl.clockwise_lon  = 0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    assert(p);

    double x, y;
    ASSERT_TRUE(p_proj_planet_fwd(p, 100000.0, 90.0, &x, &y), "ringcyl fwd origin ok");
    ASSERT_NEAR(x, 0.0, 1e-8, "ringcyl origin x=0");
    ASSERT_NEAR(y, 0.0, 1e-8, "ringcyl origin y=0");

    p_proj_planet_free(p);
    printf("PASS: RingCylindrical origin\n");
}

static void test_ring_cyl_formulas(void)
{
    /* Manual check of the formulas:
     *   x = (ring_lon - center_lon) [rad] * center_radius
     *   y = center_radius - ring_radius
     */
    PProjPlanetParams par = P_PROJ_RING_CYL_DEFAULTS;
    par.ring_cyl.center_radius  = 80000.0;
    par.ring_cyl.center_lon_deg = 0.0;
    par.ring_cyl.clockwise_lon  = 0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    assert(p);

    double x, y;
    /* ring_radius=70000, ring_lon=10° */
    p_proj_planet_fwd(p, 70000.0, 10.0, &x, &y);
    double expected_x = (10.0 * DEG2RAD) * 80000.0;
    double expected_y = 80000.0 - 70000.0;
    ASSERT_NEAR(x, expected_x, 1.0, "ringcyl x formula");
    ASSERT_NEAR(y, expected_y, 1.0, "ringcyl y formula");

    p_proj_planet_free(p);
    printf("PASS: RingCylindrical formulas\n");
}

static void test_ring_cyl_roundtrip(void)
{
    PProjPlanetParams par = P_PROJ_RING_CYL_DEFAULTS;
    par.ring_cyl.center_radius  = 120000.0;
    par.ring_cyl.center_lon_deg = 45.0;
    par.ring_cyl.clockwise_lon  = 0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    assert(p);

    double radii[]  = { 100000, 115000, 130000 };
    double lons[]   = { 20.0,   45.0,   90.0   };

    for (int i = 0; i < 3; i++) {
        double x, y, r2, l2;
        p_proj_planet_fwd(p, radii[i], lons[i], &x, &y);
        ASSERT_TRUE(p_proj_planet_inv(p, x, y, &r2, &l2), "ringcyl inv ok");
        ASSERT_NEAR(r2, radii[i], 1e-6, "ringcyl round-trip radius");
        ASSERT_NEAR(l2, lons[i],  1e-8, "ringcyl round-trip lon");
    }
    p_proj_planet_free(p);
    printf("PASS: RingCylindrical round-trip\n");
}

static void test_ring_cyl_clockwise(void)
{
    /* Clockwise longitude direction: same radius, symmetric x values. */
    PProjPlanetParams par = P_PROJ_RING_CYL_DEFAULTS;
    par.ring_cyl.center_radius  = 100000.0;
    par.ring_cyl.center_lon_deg = 0.0;
    par.ring_cyl.clockwise_lon  = 0;
    PProjPlanet *ccw = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    par.ring_cyl.clockwise_lon = 1;
    PProjPlanet *cw  = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    assert(ccw && cw);

    double x_ccw, y_ccw, x_cw, y_cw;
    p_proj_planet_fwd(ccw, 100000.0, 30.0, &x_ccw, &y_ccw);
    p_proj_planet_fwd(cw,  100000.0, 30.0, &x_cw,  &y_cw);

    /* Clockwise reverses the sign of x. */
    ASSERT_NEAR(x_ccw, -x_cw, 1e-6, "ringcyl clockwise x sign");
    /* y (radius) is unchanged by direction. */
    ASSERT_NEAR(y_ccw,  y_cw,  1e-8, "ringcyl clockwise y same");

    p_proj_planet_free(ccw);
    p_proj_planet_free(cw);
    printf("PASS: RingCylindrical clockwise\n");
}

/* ================================================================== */
/* LunarAzimuthalEqualArea                                              */
/* ================================================================== */

static void test_lunar_ea_origin(void)
{
    PProjPlanetParams par = P_PROJ_LUNAR_AZIMUTHAL_EA_DEFAULTS;
    par.lunar_ea.equatorial_radius = 1737.4;
    par.lunar_ea.max_libration_deg = 8.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_LUNAR_AZIMUTHAL_EA, &par);
    assert(p);

    double x, y;
    /* Equator, zero longitude → origin. */
    ASSERT_TRUE(p_proj_planet_fwd(p, 0.0, 0.0, &x, &y), "lunar origin fwd ok");
    ASSERT_NEAR(x, 0.0, 1e-10, "lunar origin x=0");
    ASSERT_NEAR(y, 0.0, 1e-10, "lunar origin y=0");

    p_proj_planet_free(p);
    printf("PASS: LunarAzimuthalEA origin\n");
}

static void test_lunar_ea_north_pole(void)
{
    PProjPlanetParams par = P_PROJ_LUNAR_AZIMUTHAL_EA_DEFAULTS;
    par.lunar_ea.equatorial_radius = 1737.4;
    par.lunar_ea.max_libration_deg = 8.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_LUNAR_AZIMUTHAL_EA, &par);
    assert(p);

    double x, y;
    /* North pole: should map to (0, RP) for some positive RP. */
    p_proj_planet_fwd(p, 90.0, 0.0, &x, &y);
    ASSERT_NEAR(x, 0.0, 1e-6, "lunar north pole x=0");
    ASSERT_TRUE(y > 0.0,      "lunar north pole y>0");

    p_proj_planet_free(p);
    printf("PASS: LunarAzimuthalEA north pole\n");
}

static void test_lunar_ea_roundtrip(void)
{
    PProjPlanetParams par = P_PROJ_LUNAR_AZIMUTHAL_EA_DEFAULTS;
    par.lunar_ea.equatorial_radius = 1737.4;
    par.lunar_ea.max_libration_deg = 8.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_LUNAR_AZIMUTHAL_EA, &par);
    assert(p);

    /* Spot-test several (lat, lon) pairs within the libration zone. */
    double lats[] = { 0.0,  30.0, -45.0,  70.0 };
    double lons[] = { 30.0, 15.0, -20.0,  80.0 };

    for (int i = 0; i < 4; i++) {
        double x, y, lat2, lon2;
        ASSERT_TRUE(p_proj_planet_fwd(p, lats[i], lons[i], &x, &y),
                    "lunar roundtrip fwd ok");
        ASSERT_TRUE(p_proj_planet_inv(p, x, y, &lat2, &lon2),
                    "lunar roundtrip inv ok");
        ASSERT_NEAR(lat2, lats[i], 1e-7, "lunar roundtrip lat");
        ASSERT_NEAR(lon2, lons[i], 1e-7, "lunar roundtrip lon");
    }
    p_proj_planet_free(p);
    printf("PASS: LunarAzimuthalEA round-trip\n");
}

static void test_lunar_ea_symmetry(void)
{
    /* North and south hemisphere should give y values with opposite sign. */
    PProjPlanetParams par = P_PROJ_LUNAR_AZIMUTHAL_EA_DEFAULTS;
    par.lunar_ea.equatorial_radius = 1737.4;
    par.lunar_ea.max_libration_deg = 8.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_LUNAR_AZIMUTHAL_EA, &par);
    assert(p);

    double x1, y1, x2, y2;
    p_proj_planet_fwd(p,  45.0, 0.0, &x1, &y1);
    p_proj_planet_fwd(p, -45.0, 0.0, &x2, &y2);

    ASSERT_NEAR(x1,  x2, 1e-8, "lunar sym x unchanged");
    ASSERT_NEAR(y1, -y2, 1e-8, "lunar sym y antisymmetric");

    p_proj_planet_free(p);
    printf("PASS: LunarAzimuthalEA symmetry\n");
}

/* ================================================================== */
/* UpturnedEllipsoidTransverseAzimuthal                                */
/* ================================================================== */

static void test_ueta_sphere_origin(void)
{
    /* Perfect sphere (b == a): origin at (lat=0, lon=center_lon). */
    PProjPlanetParams par = P_PROJ_UPTURNED_TA_DEFAULTS;
    par.upturned_ta.a = 1000.0;
    par.upturned_ta.b = 1000.0;
    par.upturned_ta.center_lon_deg = 0.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_UPTURNED_TA, &par);
    assert(p);

    double x, y;
    p_proj_planet_fwd(p, 0.0, 0.0, &x, &y);
    ASSERT_NEAR(x, 0.0, 1e-8, "ueta sphere origin x=0");
    ASSERT_NEAR(y, 0.0, 1e-8, "ueta sphere origin y=0");

    p_proj_planet_free(p);
    printf("PASS: UpturnedEllipsoidTA sphere origin\n");
}

static void test_ueta_sphere_roundtrip(void)
{
    /* For sphere, forward+inverse must recover the original lat/lon. */
    PProjPlanetParams par = P_PROJ_UPTURNED_TA_DEFAULTS;
    par.upturned_ta.a              = 500.0;
    par.upturned_ta.b              = 500.0;
    par.upturned_ta.center_lon_deg = 20.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_UPTURNED_TA, &par);
    assert(p);

    double lats[] = { 0.0,  20.0, -30.0,  5.0 };
    double lons[] = { 20.0, 30.0,  10.0, 40.0 };  /* within ±90° of center */

    for (int i = 0; i < 4; i++) {
        double x, y, lat2, lon2;
        ASSERT_TRUE(p_proj_planet_fwd(p, lats[i], lons[i], &x, &y),
                    "ueta sphere fwd ok");
        ASSERT_TRUE(p_proj_planet_inv(p, x, y, &lat2, &lon2),
                    "ueta sphere inv ok");
        ASSERT_NEAR(lat2, lats[i], 1e-5, "ueta sphere roundtrip lat");
        ASSERT_NEAR(lon2, lons[i], 1e-5, "ueta sphere roundtrip lon");
    }
    p_proj_planet_free(p);
    printf("PASS: UpturnedEllipsoidTA sphere round-trip\n");
}

static void test_ueta_oblate_roundtrip(void)
{
    /* Oblate body (like Eros): a=17.0, b=5.5. */
    PProjPlanetParams par = P_PROJ_UPTURNED_TA_DEFAULTS;
    par.upturned_ta.a              = 17.0;
    par.upturned_ta.b              = 5.5;
    par.upturned_ta.center_lon_deg = 0.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_UPTURNED_TA, &par);
    assert(p);

    double lats[] = {  0.0,  15.0, -10.0 };
    double lons[] = {  0.0,  30.0,  20.0 };

    for (int i = 0; i < 3; i++) {
        double x, y, lat2, lon2;
        if (!p_proj_planet_fwd(p, lats[i], lons[i], &x, &y)) continue;
        if (!p_proj_planet_inv(p, x, y, &lat2, &lon2)) continue;
        ASSERT_NEAR(lat2, lats[i], 1e-4, "ueta oblate roundtrip lat");
        ASSERT_NEAR(lon2, lons[i], 1e-4, "ueta oblate roundtrip lon");
    }
    p_proj_planet_free(p);
    printf("PASS: UpturnedEllipsoidTA oblate round-trip\n");
}

static void test_ueta_equatorial_symmetry(void)
{
    /* North and south at the same |lat| should give y values of opposite sign. */
    PProjPlanetParams par = P_PROJ_UPTURNED_TA_DEFAULTS;
    par.upturned_ta.a              = 1000.0;
    par.upturned_ta.b              = 900.0;
    par.upturned_ta.center_lon_deg = 0.0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_UPTURNED_TA, &par);
    assert(p);

    double x1, y1, x2, y2;
    p_proj_planet_fwd(p,  30.0, 0.0, &x1, &y1);
    p_proj_planet_fwd(p, -30.0, 0.0, &x2, &y2);

    ASSERT_NEAR(x1,  x2, 1e-6, "ueta equatorial symmetry x");
    ASSERT_NEAR(y1, -y2, 1e-6, "ueta equatorial symmetry y");

    p_proj_planet_free(p);
    printf("PASS: UpturnedEllipsoidTA equatorial symmetry\n");
}

/* ================================================================== */
/* OpenMP row processing                                               */
/* ================================================================== */

static void test_apply_row_omp(void)
{
    /* RingCylindrical: forward then inverse for 1000 pixels. */
    PProjPlanetParams par = P_PROJ_RING_CYL_DEFAULTS;
    par.ring_cyl.center_radius  = 80000.0;
    par.ring_cyl.center_lon_deg = 0.0;
    par.ring_cyl.clockwise_lon  = 0;
    PProjPlanet *p = p_proj_planet_create(P_PROJ_RING_CYL, &par);
    assert(p);

    int n = 1000;
    double *radii  = malloc(n * sizeof(double));
    double *lons   = malloc(n * sizeof(double));
    double *x_out  = malloc(n * sizeof(double));
    double *y_out  = malloc(n * sizeof(double));
    double *r2     = malloc(n * sizeof(double));
    double *l2     = malloc(n * sizeof(double));
    assert(radii && lons && x_out && y_out && r2 && l2);

    for (int i = 0; i < n; i++) {
        radii[i] = 60000.0 + i * 50.0;    /* 60 000 – 109 950 km */
        lons[i]  = -30.0 + 60.0 * i / n;  /* −30° to +30° */
    }

    p_proj_planet_apply_row_fwd(p, n, radii, lons,  x_out, y_out);
    p_proj_planet_apply_row_inv(p, n, x_out, y_out, r2,    l2);

    int errors = 0;
    for (int i = 0; i < n; i++) {
        if (fabs(r2[i] - radii[i]) > 1e-6 || fabs(l2[i] - lons[i]) > 1e-8) {
            fprintf(stderr, "FAIL [apply_row_omp] i=%d: r=%.6g vs %.6g, l=%.10g vs %.10g\n",
                    i, r2[i], radii[i], l2[i], lons[i]);
            if (++errors > 3) break;
        }
    }
    if (errors == 0) g_pass++; else g_fail++;

    free(radii); free(lons); free(x_out); free(y_out); free(r2); free(l2);
    p_proj_planet_free(p);
    printf("PASS: apply_row (OpenMP, %d samples)\n", n);
}

/* ================================================================== */
/* Model names                                                          */
/* ================================================================== */

static void test_names(void)
{
    PProjPlanet *a = p_proj_planet_create(P_PROJ_RING_CYL, NULL);
    PProjPlanet *b = p_proj_planet_create(P_PROJ_LUNAR_AZIMUTHAL_EA, NULL);
    PProjPlanet *c = p_proj_planet_create(P_PROJ_UPTURNED_TA, NULL);

    ASSERT_TRUE(strcmp(p_proj_planet_name(a), "RingCylindrical") == 0,
                "name RingCylindrical");
    ASSERT_TRUE(strcmp(p_proj_planet_name(b), "LunarAzimuthalEqualArea") == 0,
                "name LunarAzimuthalEqualArea");
    ASSERT_TRUE(strcmp(p_proj_planet_name(c), "UpturnedEllipsoidTransverseAzimuthal") == 0,
                "name UpturnedEllipsoidTA");

    p_proj_planet_free(a);
    p_proj_planet_free(b);
    p_proj_planet_free(c);
    printf("PASS: model names\n");
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void)
{
#ifdef _OPENMP
    printf("=== p_projection_planet tests (OpenMP: %d threads) ===\n",
           omp_get_max_threads());
#else
    printf("=== p_projection_planet tests (no OpenMP) ===\n");
#endif

    test_ring_cyl_origin();
    test_ring_cyl_formulas();
    test_ring_cyl_roundtrip();
    test_ring_cyl_clockwise();
    test_lunar_ea_origin();
    test_lunar_ea_north_pole();
    test_lunar_ea_roundtrip();
    test_lunar_ea_symmetry();
    test_ueta_sphere_origin();
    test_ueta_sphere_roundtrip();
    test_ueta_oblate_roundtrip();
    test_ueta_equatorial_symmetry();
    test_apply_row_omp();
    test_names();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
