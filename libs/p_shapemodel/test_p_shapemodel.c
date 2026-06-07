/*!
 * \file test_p_shapemodel.c
 *
 * \brief Unit tests for p_shapemodel library.
 *
 * Compile:
 *   gcc -std=c99 -DP_SHAPEMODEL_STANDALONE -fopenmp \
 *       -o test_p_shapemodel test_p_shapemodel.c p_shapemodel.c -lm
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "p_shapemodel.h"
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
/* Helpers                                                              */
/* ================================================================== */

static void normalize3(double v[3])
{
    double n=sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(n>0){v[0]/=n;v[1]/=n;v[2]/=n;}
}
static double dot3(const double a[3],const double b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

/* ================================================================== */
/* Coordinate conversion round-trip                                     */
/* ================================================================== */

static void test_coord_roundtrip(void)
{
    double lat0=23.5, lon0=137.0, r0=3390.0;
    double pt[3];
    p_shape_latlon_to_xyz(lat0, lon0, r0, pt);

    double lat1, lon1, r1;
    p_shape_xyz_to_latlon(pt, &lat1, &lon1, &r1);

    ASSERT_NEAR(lat1, lat0, 1e-10, "coord round-trip lat");
    ASSERT_NEAR(lon1, lon0, 1e-10, "coord round-trip lon");
    ASSERT_NEAR(r1,   r0,   1e-8,  "coord round-trip radius");

    /* lon=0 */
    p_shape_latlon_to_xyz(0.0, 0.0, 1.0, pt);
    p_shape_xyz_to_latlon(pt, &lat1, &lon1, &r1);
    ASSERT_NEAR(lat1, 0.0, 1e-12, "coord (0,0) lat");
    ASSERT_NEAR(lon1, 0.0, 1e-12, "coord (0,0) lon");

    /* North pole */
    p_shape_latlon_to_xyz(90.0, 42.0, 3396.2, pt);
    p_shape_xyz_to_latlon(pt, &lat1, &lon1, &r1);
    ASSERT_NEAR(lat1, 90.0,  1e-10, "coord north-pole lat");
    ASSERT_NEAR(r1,   3396.2, 1e-6, "coord north-pole radius");

    printf("PASS: coord round-trip\n");
}

/* ================================================================== */
/* Ellipsoid: local radius                                              */
/* ================================================================== */

static void test_ellipsoid_local_radius(void)
{
    /* Sphere: radius should equal a everywhere. */
    PShapeModel *s = p_shape_sphere(3390.0);
    assert(s);
    ASSERT_NEAR(p_shape_local_radius_km(s, 0.0,   0.0), 3390.0, 1e-8, "sphere lat=0");
    ASSERT_NEAR(p_shape_local_radius_km(s, 45.0, 90.0), 3390.0, 1e-8, "sphere lat=45");
    ASSERT_NEAR(p_shape_local_radius_km(s, 90.0,  0.0), 3390.0, 1e-8, "sphere pole");
    p_shape_free(s);

    /* Mars triaxial: a=3396.19, b=3396.19, c=3376.20 km. */
    PShapeModel *m = p_shape_ellipsoid(3396.19, 3396.19, 3376.20);
    assert(m);

    /* Equator: r = a = 3396.19 */
    ASSERT_NEAR(p_shape_local_radius_km(m, 0.0,  0.0), 3396.19, 1e-4, "mars equator");
    /* Pole: r = c = 3376.20 */
    ASSERT_NEAR(p_shape_local_radius_km(m, 90.0, 0.0), 3376.20, 1e-4, "mars pole");
    /* Pole result must be < equator result */
    ASSERT_TRUE(p_shape_local_radius_km(m, 90.0, 0.0) <
                p_shape_local_radius_km(m,  0.0, 0.0),
                "mars pole < equator");
    p_shape_free(m);

    printf("PASS: ellipsoid local radius\n");
}

/* ================================================================== */
/* Ellipsoid: ray intersection                                          */
/* ================================================================== */

static void test_ellipsoid_ray_intersect(void)
{
    /* Unit sphere, observer along +z axis, looking down. */
    PShapeModel *s = p_shape_sphere(1.0);
    assert(s);

    double obs[3] = { 0, 0, 3 };
    double dir[3] = { 0, 0,-1 };
    double pt[3];

    ASSERT_TRUE(p_shape_intersect_ray(s, obs, dir, pt), "sphere hit");
    ASSERT_NEAR(pt[0], 0.0, 1e-12, "sphere hit x");
    ASSERT_NEAR(pt[1], 0.0, 1e-12, "sphere hit y");
    ASSERT_NEAR(pt[2], 1.0, 1e-12, "sphere hit z (top)");

    /* Ray that misses (looking away from sphere). */
    dir[2] = 1.0;
    ASSERT_TRUE(!p_shape_intersect_ray(s, obs, dir, pt), "sphere miss");

    /* Tangent ray along y-axis: hits equator at (0,1,0). */
    obs[0]=0; obs[1]=3; obs[2]=0;
    dir[0]=0; dir[1]=-1; dir[2]=0;
    ASSERT_TRUE(p_shape_intersect_ray(s, obs, dir, pt), "sphere tangent hit");
    ASSERT_NEAR(sqrt(pt[0]*pt[0]+pt[1]*pt[1]+pt[2]*pt[2]), 1.0, 1e-10,
                "sphere tangent hit on surface");

    p_shape_free(s);

    /* Mars ellipsoid: observer on x-axis, look toward centre. */
    PShapeModel *mars = p_shape_ellipsoid(3396.19, 3396.19, 3376.20);
    obs[0]=10000; obs[1]=0; obs[2]=0;
    dir[0]=-1; dir[1]=0; dir[2]=0;
    ASSERT_TRUE(p_shape_intersect_ray(mars, obs, dir, pt), "mars equator hit");
    ASSERT_NEAR(pt[0], 3396.19, 1e-4, "mars equator hit x");
    ASSERT_NEAR(pt[1], 0.0,     1e-8, "mars equator hit y");
    ASSERT_NEAR(pt[2], 0.0,     1e-8, "mars equator hit z");
    p_shape_free(mars);

    printf("PASS: ellipsoid ray intersection\n");
}

/* ================================================================== */
/* Ellipsoid: surface normal                                            */
/* ================================================================== */

static void test_ellipsoid_normal(void)
{
    PShapeModel *s = p_shape_sphere(3390.0);
    assert(s);

    /* On a sphere, the surface normal at any point must equal the
     * unit radial vector (pointing outward from centre). */
    double pts[4][3] = {
        { 3390, 0, 0 }, { 0, 3390, 0 }, { 0, 0, 3390 },
        { 3390*cos(30*DEG2RAD)*cos(45*DEG2RAD),
          3390*cos(30*DEG2RAD)*sin(45*DEG2RAD),
          3390*sin(30*DEG2RAD) }
    };
    for (int i = 0; i < 4; i++) {
        double n[3];
        p_shape_normal(s, pts[i], n);

        /* n must be unit length */
        ASSERT_NEAR(sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]), 1.0, 1e-10,
                    "sphere normal unit length");

        /* n must be collinear with radial vector */
        double rad[3] = { pts[i][0]/3390, pts[i][1]/3390, pts[i][2]/3390 };
        ASSERT_NEAR(fabs(dot3(n, rad)), 1.0, 1e-10, "sphere normal == radial");
    }
    p_shape_free(s);

    /* Triaxial ellipsoid: normal is not parallel to radial unless on axis. */
    PShapeModel *e = p_shape_ellipsoid(6378.1, 6356.8, 6356.8);
    double pt[3]  = { 6378.1, 0, 0 };  /* equatorial x */
    double nrm[3];
    p_shape_normal(e, pt, nrm);
    ASSERT_NEAR(nrm[0], 1.0, 1e-8, "ellipsoid x-axis normal x");
    ASSERT_NEAR(nrm[1], 0.0, 1e-8, "ellipsoid x-axis normal y");
    ASSERT_NEAR(nrm[2], 0.0, 1e-8, "ellipsoid x-axis normal z");
    p_shape_free(e);

    printf("PASS: ellipsoid surface normal\n");
}

/* ================================================================== */
/* Angle calculations                                                   */
/* ================================================================== */

static void test_angles(void)
{
    /* Nadir geometry: observer directly above, sun directly above.
     * Surface point at top of unit sphere, normal = (0,0,1). */
    double pt[3]     = { 0, 0, 1 };
    double normal[3] = { 0, 0, 1 };
    double obs[3]    = { 0, 0, 10 };
    double sun[3]    = { 0, 0, 10 };

    /* Incidence and emission should both be 0 (nadir, overhead sun). */
    ASSERT_NEAR(p_shape_incidence_angle(pt, normal, sun), 0.0, 1e-8,
                "nadir incidence=0");
    ASSERT_NEAR(p_shape_emission_angle(pt, normal, obs), 0.0, 1e-8,
                "nadir emission=0");
    /* Phase angle: both vectors point in same direction → 0. */
    ASSERT_NEAR(p_shape_phase_angle(pt, obs, sun), 0.0, 1e-8,
                "nadir phase=0");

    /* 90-degree incidence: sun in the tangent plane of the surface.
     * pt=(0,0,1), normal=(0,0,1). For incidence=90, sun-pt must be
     * perpendicular to normal, so sun must have the same z as pt. */
    sun[0]=10; sun[1]=0; sun[2]=1.0; /* z equals pt[2] → direction is horizontal */
    ASSERT_NEAR(p_shape_incidence_angle(pt, normal, sun), 90.0, 1e-6,
                "horizon incidence=90");

    /* Oblique emission: observer at 45° from normal.
     * pt=(0,0,1), normal=(0,0,1). Obs must satisfy:
     *   cos(45) = dot(normal, (obs-pt)/|obs-pt|)
     * Put obs in the xz-plane at pt + r*(sin45, 0, cos45):
     *   obs = (0+r/√2, 0, 1+r/√2) for large r. */
    double rr = 1000.0;
    obs[0] = rr * sin(45.0*DEG2RAD);
    obs[1] = 0.0;
    obs[2] = pt[2] + rr * cos(45.0*DEG2RAD);
    ASSERT_NEAR(p_shape_emission_angle(pt, normal, obs), 45.0, 1e-4,
                "45deg emission");

    /* Phase angle between two vectors 90° apart from the surface point. */
    double obs2[3]  = { 0, 0, 10 };   /* directly above */
    double sun2[3]  = { 10, 0, 0 };   /* due east */
    /* cos(phase) = dot(obs-pt, sun-pt)/|..| = dot((0,0,9),(10,0,-1))/norm */
    double v1[3]={ obs2[0]-pt[0], obs2[1]-pt[1], obs2[2]-pt[2] };
    double v2[3]={ sun2[0]-pt[0], sun2[1]-pt[1], sun2[2]-pt[2] };
    normalize3(v1); normalize3(v2);
    double expected_phase = acos(dot3(v1,v2)) * RAD2DEG;
    ASSERT_NEAR(p_shape_phase_angle(pt, obs2, sun2), expected_phase, 1e-6,
                "phase angle formula");

    printf("PASS: angle calculations\n");
}

/* ================================================================== */
/* DEM shape: constant-radius sphere DEM                                */
/* ================================================================== */

static double constant_radius_fn(double lat, double lon, void *ud)
{
    (void)lat; (void)lon;
    return *(double *)ud;
}

static void test_dem_constant(void)
{
    double r = 3390.0;
    /* DEM that always returns r should behave identically to a sphere. */
    PShapeModel *dem  = p_shape_dem(constant_radius_fn, &r, r, r, r, 0.0);
    PShapeModel *sph  = p_shape_sphere(r);
    assert(dem && sph);

    double obs[3] = { 0, 0, 10000 };
    double dir[3] = { 0, 0, -1 };
    double pt_d[3], pt_s[3];

    int hit_d = p_shape_intersect_ray(dem, obs, dir, pt_d);
    int hit_s = p_shape_intersect_ray(sph, obs, dir, pt_s);

    ASSERT_TRUE(hit_d && hit_s, "DEM const hit");
    ASSERT_NEAR(pt_d[2], r, 1.0,  /* DEM iteration: convergence to 1e-6 km */
                "DEM const intersection z");

    /* Local radius at equator must equal r. */
    ASSERT_NEAR(p_shape_local_radius_km(dem, 0.0, 0.0), r, 1e-10,
                "DEM const local radius");

    p_shape_free(dem);
    p_shape_free(sph);
    printf("PASS: DEM constant-radius sphere\n");
}

/* ================================================================== */
/* DEM shape: normal via finite difference                              */
/* ================================================================== */

/* A sinusoidal DEM: r = R0 + A*sin(lat_rad)*cos(lon_rad) */
typedef struct { double R0, A; } SinDemParams;

static double sin_dem_fn(double lat, double lon, void *ud)
{
    SinDemParams *p = (SinDemParams *)ud;
    return p->R0 + p->A * sin(lat * DEG2RAD) * cos(lon * DEG2RAD);
}

static void test_dem_normal(void)
{
    SinDemParams sp = { .R0 = 3390.0, .A = 5.0 };
    PShapeModel *dem = p_shape_dem(sin_dem_fn, &sp, 3390.0, 3390.0, 3390.0, 0.01);
    assert(dem);

    /* Get normal at equator, lon=0. */
    double pt[3];
    p_shape_latlon_to_xyz(0.0, 0.0, 3390.0, pt);
    double n[3];
    p_shape_normal(dem, pt, n);

    /* Normal must be unit length. */
    ASSERT_NEAR(sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]), 1.0, 1e-8,
                "DEM normal unit length");

    /* Normal must point generally outward (positive dot with position). */
    ASSERT_TRUE(n[0]*pt[0]+n[1]*pt[1]+n[2]*pt[2] > 0.0,
                "DEM normal outward");

    p_shape_free(dem);
    printf("PASS: DEM normal (finite difference)\n");
}

/* ================================================================== */
/* Plane shape                                                          */
/* ================================================================== */

static void test_plane(void)
{
    PShapeModel *pl = p_shape_plane();
    assert(pl);

    /* Observer above the plane, looking down: hit at z=0. */
    double obs[3] = { 1000, 2000, 500 };
    double dir[3] = { 0, 0, -1 };
    double pt[3];

    ASSERT_TRUE(p_shape_intersect_ray(pl, obs, dir, pt), "plane hit");
    ASSERT_NEAR(pt[2], 0.0, 1e-10, "plane hit z=0");
    ASSERT_NEAR(pt[0], 1000.0, 1e-8, "plane hit x unchanged");
    ASSERT_NEAR(pt[1], 2000.0, 1e-8, "plane hit y unchanged");

    /* Normal must be (0,0,1) when observer is above. */
    double n[3];
    p_shape_normal(pl, pt, n);
    ASSERT_NEAR(n[2],  1.0, 1e-12, "plane normal z=+1 above");

    /* Observer below z=0. */
    obs[2] = -500;
    dir[2] = +1;
    ASSERT_TRUE(p_shape_intersect_ray(pl, obs, dir, pt), "plane below hit");

    /* Looking away from plane: no intersection. */
    dir[2] = -1;
    ASSERT_TRUE(!p_shape_intersect_ray(pl, obs, dir, pt), "plane below miss");

    /* Parallel ray: no intersection. */
    obs[2] = 1; dir[0]=1; dir[1]=0; dir[2]=0;
    ASSERT_TRUE(!p_shape_intersect_ray(pl, obs, dir, pt), "plane parallel miss");

    p_shape_free(pl);
    printf("PASS: plane shape\n");
}

/* ================================================================== */
/* apply_row with OpenMP                                                */
/* ================================================================== */

static void test_apply_row_omp(void)
{
    PShapeModel *mars = p_shape_ellipsoid(3396.19, 3396.19, 3376.20);
    assert(mars);

    int n = 360;
    /* Observer far above north pole. */
    double obs[3] = { 0, 0, 50000 };
    /* Sun far along +x axis. */
    double sun[3] = { 500000, 0, 0 };

    /* Build look directions: scan across a range of angles from nadir. */
    double *dirs = (double *)malloc(n * 3 * sizeof(double));
    assert(dirs);
    for (int i = 0; i < n; i++) {
        /* Angle from −30° to +30° in the x-z plane. */
        double ang = (-30.0 + 60.0 * i / (n-1)) * DEG2RAD;
        dirs[3*i+0] = sin(ang);
        dirs[3*i+1] = 0.0;
        dirs[3*i+2] = -cos(ang);
    }

    PShapeResult *res = (PShapeResult *)malloc(n * sizeof(PShapeResult));
    assert(res);

    p_shape_apply_row(mars, n, obs, dirs, sun, res);

    int hits = 0, misses = 0;
    for (int i = 0; i < n; i++) {
        if (res[i].incidence != res[i].incidence) { /* NaN */
            misses++;
        } else {
            hits++;
            /* Incidence and emission must be in [0, 180]. */
            ASSERT_TRUE(res[i].incidence >= 0.0 && res[i].incidence <= 180.0,
                        "apply_row incidence range");
            ASSERT_TRUE(res[i].emission  >= 0.0 && res[i].emission  <= 180.0,
                        "apply_row emission range");
            ASSERT_TRUE(res[i].phase     >= 0.0 && res[i].phase     <= 180.0,
                        "apply_row phase range");
            ASSERT_TRUE(res[i].local_radius > 3370.0 && res[i].local_radius < 3400.0,
                        "apply_row radius in Mars range");
        }
    }
    ASSERT_TRUE(hits > 0,   "apply_row some hits");
    ASSERT_TRUE(misses > 0, "apply_row some misses (outer scan)");

    /* Spot-check near-nadir pixel (centre of scan is ~0.08° from vertical,
     * so emission ≈ small, within 5° given polar flattening effects). */
    int mid = n / 2;
    if (res[mid].emission == res[mid].emission) { /* not NaN */
        ASSERT_NEAR(res[mid].emission, 0.0, 5.0,
                    "apply_row near-nadir emission <5deg");
    }

    free(dirs); free(res);
    p_shape_free(mars);
    printf("PASS: apply_row (OpenMP, %d samples)\n", n);
}

/* ================================================================== */
/* Model names                                                          */
/* ================================================================== */

static void test_names(void)
{
    PShapeModel *e = p_shape_ellipsoid(1,1,1);
    PShapeModel *p = p_shape_plane();
    double r = 1.0;
    PShapeModel *d = p_shape_dem(constant_radius_fn, &r, 1,1,1, 0);

    ASSERT_TRUE(strcmp(p_shape_name(e), "Ellipsoid") == 0, "name ellipsoid");
    ASSERT_TRUE(strcmp(p_shape_name(p), "Plane")     == 0, "name plane");
    ASSERT_TRUE(strcmp(p_shape_name(d), "DEM")       == 0, "name dem");

    p_shape_free(e); p_shape_free(p); p_shape_free(d);
    printf("PASS: model names\n");
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void)
{
#ifdef _OPENMP
    printf("=== p_shapemodel tests (OpenMP: %d threads) ===\n",
           omp_get_max_threads());
#else
    printf("=== p_shapemodel tests (no OpenMP) ===\n");
#endif

    test_coord_roundtrip();
    test_ellipsoid_local_radius();
    test_ellipsoid_ray_intersect();
    test_ellipsoid_normal();
    test_angles();
    test_dem_constant();
    test_dem_normal();
    test_plane();
    test_apply_row_omp();
    test_names();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
