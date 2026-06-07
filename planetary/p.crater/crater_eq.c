/****************************************************************************
 *
 * MODULE:       p.crater (crater_eq.c)
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Crater scaling equations - Melosh 1989 / Holsapple 1993 /
 *               Pike 1980 / Schmidt & Holsapple 1982.
 *
 *               Inputs in SI units (kg, m, s, J); angles in radians.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <math.h>
#include "crater_eq.h"
#include "planet_db.h"  /* for P_CRATER_TT_COMPETENT et al. */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* TNT specific energy: 4.184e9 J / ton -> 4.184e12 J / kt */
#define TNT_J_PER_KT 4.184e12

/* Lunar simple-to-complex transition diameter [m] (Pike 1980). */
#define DSC_MOON 18000.0
#define G_MOON   1.622

/* ================================================================== */
/* Energy                                                              */
/* ================================================================== */

double p_crater_mass(double rho_p, double L)
{
    double r = 0.5 * L;
    return rho_p * (4.0 / 3.0) * M_PI * r * r * r;
}

double p_crater_kinetic_energy(double rho_p, double L, double Vi)
{
    double M = p_crater_mass(rho_p, L);
    return 0.5 * M * Vi * Vi;
}

double p_crater_tnt_kt(double W_J)
{
    return W_J / TNT_J_PER_KT;
}

/* ================================================================== */
/* Forward scaling                                                     */
/* ================================================================== */

double p_crater_gault_Dat(double W, double rho_p, double rho_t,
                          double theta, int target_type)
{
    double Dat;
    double s = sin(theta);
    if (s <= 0.0) s = 1e-6;

    if (target_type == P_CRATER_TT_COMPETENT) {
        /* For craters up to 10 m + solid target rock (Gault 1974). */
        Dat = 0.015 * pow(rho_p, 1.0/6.0) * pow(rho_t, -0.5) *
              pow(W, 0.37) * pow(s, 2.0/3.0);
        if (Dat > 10.0) {
            Dat = 0.25 * pow(rho_p, 1.0/6.0) * pow(rho_t, -0.5) *
                  pow(W, 0.29) * pow(s, 1.0/3.0);
            if (Dat > 100.0) {
                Dat = 0.27 * pow(rho_p, 1.0/6.0) * pow(rho_t, -0.5) *
                      pow(W, 0.28) * pow(s, 1.0/3.0);
            }
        }
    } else {
        Dat = 0.25 * pow(rho_p, 1.0/6.0) * pow(rho_t, -0.5) *
              pow(W, 0.29) * pow(s, 1.0/3.0);
        if (Dat > 100.0) {
            Dat = 0.27 * pow(rho_p, 1.0/6.0) * pow(rho_t, -0.5) *
                  pow(W, 0.28) * pow(s, 1.0/3.0);
        }
    }
    return Dat;
}

double p_crater_yield_Dat(double W, double rho_p, double rho_t, double L)
{
    return 0.0133 * pow(W, 1.0/3.4) +
           1.51 * sqrt(rho_p / rho_t) * L;
}

double p_crater_pi_Dat(double W, double rho_p, double rho_t, double L, double g)
{
    return 1.8 * pow(rho_p, 0.11) * pow(rho_t, -1.0/3.0) *
           pow(g, -0.22) * pow(L, 0.13) * pow(W, 0.22);
}

/* ================================================================== */
/* Backward scaling                                                    */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* The backward formulas invert the forward ones analytically by       */
/* substituting the kinetic-energy expression                          */
/*                                                                     */
/*     W = (pi/12) * rho_p * L^3 * V^2                                 */
/*                                                                     */
/* into the Melosh (1989) forward relations. Each forward formula has  */
/* the structure   Dat = K(rho_p, rho_t, g, V, theta) * L^alpha , so   */
/* the inverse is a single-power-law expression.                       */
/*                                                                     */
/* NOTE: the original r.crater addon (grass-addons/src/raster/r.crater) */
/* used Vfac = (pi/3) V^2 with a (0.5)^N correction that introduces a  */
/* factor-of-many error (~11x for Pi-scaling at typical asteroid       */
/* densities). p.crater 0.4.9+ uses the corrected analytical inverse.  */
/* ------------------------------------------------------------------ */

static double pi_over_12_pow(double exponent)
{
    return pow(M_PI / 12.0, exponent);
}

double p_crater_gault_L(double Dat, double Vi, double rho_p, double rho_t,
                         double theta, int target_type)
{
    double s = sin(theta);
    if (s <= 0.0) s = 1e-6;

    /* Three Gault regimes, mirroring the forward formula branches.
     * Forward kernel: Dat = c * rho_p^(1/6) * rho_t^(-1/2) *
     *                      W^q * sin(theta)^p
     * Substituting W: factor (pi/12)^q * rho_p^q * V^(2q) * L^(3q)
     *                emerges, multiplied by the c, rho_p^(1/6) prefix. */

    /* Helper macro for the analytical inverse of a Gault branch:
     *   K = c * (pi/12)^q * rho_p^(1/6+q) * rho_t^(-1/2) * V^(2q)
     *       * sin(theta)^p
     *   alpha = 3*q (since W brings L^3 to the power q)
     *   L = (Dat / K)^(1/alpha)                                       */
#define GAULT_INVERSE(C, Q, SIN_EXP) \
    pow( (Dat) / ((C) * pi_over_12_pow(Q) * \
                  pow(rho_p, (1.0/6.0) + (Q)) * pow(rho_t, -0.5) * \
                  pow(Vi, 2.0 * (Q)) * pow(s, SIN_EXP)), \
         1.0 / (3.0 * (Q)) )

    if (target_type == P_CRATER_TT_COMPETENT) {
        if (Dat < 10.0)        /* solid rock, < 10 m */
            return GAULT_INVERSE(0.015, 0.37, 2.0/3.0);
        if (Dat < 100.0)       /* loose mixed, 10-100 m */
            return GAULT_INVERSE(0.25,  0.29, 1.0/3.0);
        /* large craters, any target, >= 100 m */
        return GAULT_INVERSE(0.27,  0.28, 1.0/3.0);
    }
    if (Dat < 100.0)
        return GAULT_INVERSE(0.25,  0.29, 1.0/3.0);
    return GAULT_INVERSE(0.27,  0.28, 1.0/3.0);

#undef GAULT_INVERSE
}

double p_crater_yield_L(double Vi, double rho_p, double rho_t, double Dat)
{
    /* Forward (Nordyke 1962):
     *   Dat = 0.0133 * W^(1/3.4) + 1.51 * (rho_p/rho_t)^0.5 * L
     *
     * Substituting W = (pi/12) rho_p L^3 V^2:
     *   Dat = A * L^(3/3.4) + B * L
     *   A   = 0.0133 * ((pi/12) rho_p V^2)^(1/3.4)
     *   B   = 1.51 * sqrt(rho_p / rho_t)
     *
     * Exponent 3/3.4 = 0.882 mixes with L^1, so the inverse has no
     * closed form. Solve via bisection on f(L) = A L^0.882 + B L - Dat. */
    double A = 0.0133 * pow((M_PI / 12.0) * rho_p * Vi * Vi, 1.0 / 3.4);
    double B = 1.51 * sqrt(rho_p / rho_t);

    /* Bracket: lower bound L=1 cm (Dat > 0 always), upper bound from
     * the linear term alone (B*L = Dat) which over-estimates L.       */
    double lo = 1e-2;
    double hi = Dat / fmax(B, 1e-12) + 1.0;
    /* Make sure f(hi) > 0 */
    int safety = 32;
    while ((A * pow(hi, 3.0/3.4) + B * hi - Dat) < 0.0 && safety-- > 0)
        hi *= 2.0;

    for (int iter = 0; iter < 80; iter++) {
        double mid = 0.5 * (lo + hi);
        double f   = A * pow(mid, 3.0/3.4) + B * mid - Dat;
        if (f > 0.0) hi = mid; else lo = mid;
        if ((hi - lo) < 1e-6 * fmax(hi, 1.0)) break;
    }
    return 0.5 * (lo + hi);
}

double p_crater_pi_L(double Vi, double rho_p, double rho_t, double Dat, double g)
{
    /* Forward (Melosh 1989 eq 7.8.4):
     *   Dat = 1.8 * rho_p^0.11 * rho_t^(-1/3) * g^(-0.22)
     *         * L^0.13 * W^0.22
     * Substituting W = (pi/12) rho_p L^3 V^2:
     *   Dat = 1.8 * (pi/12)^0.22 * rho_p^(0.33) * rho_t^(-1/3)
     *         * g^(-0.22) * V^(0.44) * L^(0.79)
     * Inverse:
     *   L = (Dat / K)^(1/0.79)                                       */
    double K = 1.8 * pi_over_12_pow(0.22) *
               pow(rho_p, 0.33) * pow(rho_t, -1.0/3.0) *
               pow(g, -0.22)    * pow(Vi, 0.44);
    return pow(Dat / K, 1.0 / 0.79);
}

/* ================================================================== */
/* Final crater diameter and depth                                     */
/* ================================================================== */

double p_crater_simple_complex_D(double g)
{
    if (g <= 0.0) return DSC_MOON;
    return DSC_MOON * (G_MOON / g);
}

double p_crater_simple_complex_D_body(double g, double dsc_km_measured)
{
    /* If the body database carries a measured Dsc, use it (converted
     * to metres). Otherwise fall back to the 1/g analytic scaling.   */
    if (dsc_km_measured > 0.0) return dsc_km_measured * 1000.0;
    return p_crater_simple_complex_D(g);
}

double p_crater_final_diameter(double Dat, double g,
                                 double dsc_km_measured)
{
    double Dsc = p_crater_simple_complex_D_body(g, dsc_km_measured);
    double Df_simple = 1.25 * Dat;
    if (Df_simple < Dsc) {
        /* Stays in simple regime: rim-uplift + breccia-lens fills, +25%. */
        return Df_simple;
    }
    /* Complex regime (Croft 1985 / Holsapple 1993):
     * Df = 1.17 * Dat^1.13 / Dsc^0.13                           */
    return 1.17 * pow(Dat, 1.13) / pow(Dsc, 0.13);
}

double p_crater_depth_ratio(double D, double g, double dD_simple,
                              double dsc_km_measured)
{
    if (dD_simple <= 0.0) dD_simple = 0.196;
    double Dsc = p_crater_simple_complex_D_body(g, dsc_km_measured);
    if (D < Dsc) return dD_simple;
    double r = 0.05 + (dD_simple - 0.05) * sqrt(Dsc / D);
    if (r > dD_simple) r = dD_simple;
    if (r < 0.02)      r = 0.02;
    return r;
}

double p_crater_depth(double D, double g, double dD_simple,
                       double dsc_km_measured)
{
    return D * p_crater_depth_ratio(D, g, dD_simple, dsc_km_measured);
}
