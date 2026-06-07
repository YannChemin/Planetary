/*!
 * \file impactor_points.h
 * \brief Forward-mode synthesis of impact craters from a point vector
 *        of impactor locations + attributes.
 *
 * For each point feature in the input vector, p.crater reads five
 * attributes (velocity, impact angle from local surface, azimuth of
 * downrange direction, impactor density, impactor diameter) and a
 * DEM raster, then generates one or more polygon features in the
 * output vector representing:
 *
 *   - a circular primary crater (theta >= ~30 deg from local surface,
 *     i.e. near-normal impact),
 *   - an elliptical primary crater (5 deg <= theta < 30 deg, oblique
 *     impact with major axis aligned with the downrange azimuth),
 *   - a chain of decreasing-size craters along the trajectory when
 *     theta < ~5 deg (grazing impact -> projectile skips, breaks up,
 *     and produces a ricochet trail).
 *
 * The aspect-ratio law (b/a = sin(theta)^(1/3)) follows Pierazzo &
 * Melosh (2000) "Understanding Oblique Impacts", Annual Reviews of
 * Earth and Planetary Sciences 28:141-167.
 * The ricochet chain is modelled after Schultz & Gault (1990).
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 * \copyright The Unlicense (SPDX-License-Identifier: Unlicense)
 */

#ifndef P_CRATER_IMPACTOR_POINTS_H
#define P_CRATER_IMPACTOR_POINTS_H

#include <grass/vector.h>
#include <grass/dbmi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Attribute-column overrides supplied from the command line. */
typedef struct {
    const char *col_velocity;     /*!< default "velocity"      */
    const char *col_angle;        /*!< default "impact_angle"  */
    const char *col_azimuth;      /*!< default "azimuth"       */
    const char *col_density;      /*!< default "density"       */
    const char *col_diameter;     /*!< default "diameter"      */
    int         density_g_cm3;    /*!< 1 = g/cm^3, 0 = kg/m^3  */
} PCraterImpactorCols;

/*! Body parameters for the run. */
typedef struct {
    double g;                     /*!< surface gravity (m/s^2)   */
    double rho_t_default;         /*!< target surface density    */
    int    target_type_default;   /*!< Gault target type 1/2/3   */
    double dD_simple;             /*!< simple-crater d/D fallback */
    int    fd_dd_map;             /*!< open d/D raster fd, or -1  */
    double dsc_km;                /*!< measured Dsc [km], 0=fallback */
} PCraterBodyCtx;

/*!
 * \brief Run the impactor-points synthesis mode end-to-end.
 *
 * Opens the input point vector, reads attribute columns named in
 * \a cols, samples \a dem_name (mandatory) at each point, computes
 * crater geometry and writes one or more polygons per impactor to a
 * new output vector \a out_name. All physics outputs (Df, depth,
 * energy, ...) are written as attributes on each polygon.
 *
 * \return EXIT_SUCCESS on success, EXIT_FAILURE on any setup error.
 */
int p_crater_run_impactor_points(const char *impactors_name,
                                  const char *layer_name,
                                  const char *out_name,
                                  const char *dem_name,
                                  const PCraterImpactorCols *cols,
                                  const PCraterBodyCtx *body);

#ifdef __cplusplus
}
#endif

#endif /* P_CRATER_IMPACTOR_POINTS_H */
