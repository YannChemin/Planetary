/*!
 * \file crater_eq.h
 * \brief Crater scaling equations for p.crater.
 *
 * Extends the original r.crater equations (Melosh 1989, ch.7) with:
 *  - Final crater diameter (apparent transient + collapse correction)
 *    after Holsapple 1993 and Kring 2007
 *  - Complex/simple crater transition (Pike 1980, Melosh 1989)
 *  - Energy / TNT-equivalent conversion
 *
 * All densities in kg/m^3, all lengths in metres, velocities in m/s,
 * energies in Joules.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#ifndef GRASS_P_CRATER_EQ_H
#define GRASS_P_CRATER_EQ_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Energy                                                              */
/* ------------------------------------------------------------------ */

/*! \brief Spherical impactor mass [kg]: M = (4/3) * pi * (L/2)^3 * rho_p. */
double p_crater_mass(double rho_p, double L);

/*! \brief Kinetic energy [J] of impactor. Vi in m/s. */
double p_crater_kinetic_energy(double rho_p, double L, double Vi);

/*! \brief Convert energy [J] to TNT equivalent [kilotons]. */
double p_crater_tnt_kt(double W_J);

/* ------------------------------------------------------------------ */
/* Forward scaling: known projectile -> apparent transient diameter   */
/* ------------------------------------------------------------------ */

/*!
 * \brief Gault scaling (Gault 1974) for apparent transient diameter.
 *
 * Returns Dat in metres. Uses branching by computed Dat to switch
 * between solid-rock, regolith, and large-crater regimes.
 *
 * \param W kinetic energy [J]
 * \param rho_p projectile density [kg/m^3]
 * \param rho_t target density [kg/m^3]
 * \param theta impact angle from horizontal [radians]
 * \param target_type 1=water/ice, 2=loose, 3=competent
 */
double p_crater_gault_Dat(double W, double rho_p, double rho_t,
                          double theta, int target_type);

/*! \brief Yield scaling (Nordyke 1962). Returns Dat [m]. L in metres. */
double p_crater_yield_Dat(double W, double rho_p, double rho_t, double L);

/*!
 * \brief Pi-group scaling (Schmidt & Holsapple 1982, Melosh 1989 eq. 7.8.4).
 *
 * \param W kinetic energy [J]
 * \param rho_p projectile density [kg/m^3]
 * \param rho_t target density [kg/m^3]
 * \param L projectile diameter [m]
 * \param g surface gravity [m/s^2]
 */
double p_crater_pi_Dat(double W, double rho_p, double rho_t, double L, double g);

/* ------------------------------------------------------------------ */
/* Backward scaling: known apparent transient diameter -> projectile  */
/* ------------------------------------------------------------------ */

double p_crater_gault_L(double Dat, double Vi, double rho_p, double rho_t,
                         double theta, int target_type);
double p_crater_yield_L(double Vi, double rho_p, double rho_t, double Dat);
double p_crater_pi_L   (double Vi, double rho_p, double rho_t, double Dat, double g);

/* ------------------------------------------------------------------ */
/* Apparent transient -> final crater diameter (collapse correction)  */
/* ------------------------------------------------------------------ */

/*!
 * \brief Simple-to-complex crater transition diameter [m] for a body.
 *
 * Empirical fit Dsc proportional to 1/g (Pike 1980, Melosh 1989):
 *   Dsc(g) = Dsc_Moon * (g_Moon / g)
 * with Dsc_Moon = 18 km (lunar value). Used as a fallback when the
 * body's measured Dsc is unknown.
 *
 * \param g surface gravity [m/s^2]
 * \return transition diameter [m]
 */
double p_crater_simple_complex_D(double g);

/*!
 * \brief Body-aware simple-to-complex transition diameter [m].
 *
 * If \a dsc_km_measured > 0, returns it (converted to metres). Else
 * falls back to the 1/g analytic scaling.
 *
 * \param g surface gravity [m/s^2]
 * \param dsc_km_measured measured Dsc from the PCraterBody database
 *        (0.0 if no measurement is available for this body)
 */
double p_crater_simple_complex_D_body(double g, double dsc_km_measured);

/*!
 * \brief Convert apparent transient Dat to final crater diameter Df [m].
 *
 * Uses the standard scaling:
 *  - Simple bowl crater (D < Dsc):   Df = 1.25 * Dat (Melosh 1989 ch. 8)
 *  - Complex/peak-ring (D >= Dsc):   Df = 1.17 * Dat^1.13 / Dsc^0.13
 *    (Croft 1985 fit, Holsapple 1993)
 *
 * \param Dat apparent transient diameter [m]
 * \param g surface gravity [m/s^2]
 * \param dsc_km_measured measured Dsc [km] for body (0 = use 1/g fallback)
 */
double p_crater_final_diameter(double Dat, double g, double dsc_km_measured);

/*!
 * \brief Estimated depth-to-diameter ratio for fresh craters.
 *
 * Simple craters: body-specific value from PCraterBody::dD_simple
 *   (e.g. 0.196 Moon, 0.150 Mars, 0.180 Mercury, 0.130 Earth, 0.150 icy moons).
 * Complex craters: scaled down with crater size as 0.05 + (dD_simple-0.05) *
 *   sqrt(Dsc/D), so the complex regime smoothly transitions from the simple
 *   value at D = Dsc to ~0.05 for very large craters (Pike 1980).
 *
 * \param D          crater diameter [m]
 * \param g          surface gravity [m/s^2]
 * \param dD_simple  body-specific simple-crater d/D (0..1); pass 0.196 for
 *                   the legacy lunar default if unknown.
 * \param dsc_km_measured  measured Dsc [km] (0 = use 1/g fallback).
 */
double p_crater_depth_ratio(double D, double g, double dD_simple,
                              double dsc_km_measured);

/*!
 * \brief Estimated crater depth [m] from final diameter.
 *
 * \param dD_simple body-specific simple-crater d/D ratio (see above).
 * \param dsc_km_measured measured Dsc [km] (0 = use 1/g fallback).
 */
double p_crater_depth(double D, double g, double dD_simple,
                       double dsc_km_measured);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_CRATER_EQ_H */
