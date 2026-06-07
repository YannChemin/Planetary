/*!
 * \file planet_db.h
 * \brief Built-in planetary body database for p.crater.
 *
 * Provides physical constants for major Solar System bodies relevant
 * to impact crater scaling: surface gravity, mean radius, bulk density,
 * typical surface (target) density, and dominant target-material type
 * code used by Gault scaling (1=liquid water/ice, 2=loose sand/regolith,
 * 3=competent rock / saturated soil).
 *
 * Numeric values are taken from the IAU Working Group on Cartographic
 * Coordinates and Rotational Elements 2015 report (Archinal et al. 2018)
 * for radii/gravity, and from peer-reviewed regolith / crustal density
 * studies cited in planet_db.c.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense - public domain dedication
 *            (SPDX-License-Identifier: Unlicense)
 */

#ifndef GRASS_P_CRATER_PLANET_DB_H
#define GRASS_P_CRATER_PLANET_DB_H

#ifdef __cplusplus
extern "C" {
#endif

/*! Target material types used by Gault scaling. */
typedef enum {
    P_CRATER_TT_WATER_ICE   = 1,  /*!< Liquid water or pure water ice */
    P_CRATER_TT_LOOSE       = 2,  /*!< Loose sand / regolith */
    P_CRATER_TT_COMPETENT   = 3   /*!< Competent rock / saturated soil */
} PCraterTargetType;

/*! Planetary body record. */
typedef struct {
    const char *name;             /*!< Body name (lowercase, "moon", "mars", ...) */
    double      g;                /*!< Surface gravity [m/s^2] */
    double      radius_km;        /*!< Mean radius [km] */
    double      bulk_density;     /*!< Bulk density [kg/m^3] */
    double      surface_density;  /*!< Typical surface (target) density [kg/m^3] */
    int         target_type;      /*!< Default Gault target type (see enum) */
    double      dD_simple;        /*!< Depth/Diameter ratio for SIMPLE craters
                                       on this body (Pike 1977/1980/1988 etc.) */
    double      dsc_km;           /*!< Measured simple-to-complex transition
                                       diameter [km] (Pike 1980/1988/Schaber
                                       1992/Schenk 2002). 0.0 = "use the
                                       analytic 1/g scaling fallback".         */
    const char *description;      /*!< Short description for help / log output */
} PCraterBody;

/*!
 * \brief Look up planetary body by name (case-insensitive).
 * \param name body name (e.g. "moon", "mars", "europa")
 * \return Pointer to immutable record, or NULL if unknown.
 */
const PCraterBody *p_crater_body_lookup(const char *name);

/*!
 * \brief Comma-separated list of known body names.
 * \return Static string suitable for option->options.
 */
const char *p_crater_body_options(void);

/*!
 * \brief Total number of bodies in the database.
 */
int p_crater_body_count(void);

/*!
 * \brief Iterate database; returns NULL when index out of range.
 */
const PCraterBody *p_crater_body_at(int index);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_CRATER_PLANET_DB_H */
