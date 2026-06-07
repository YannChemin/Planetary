/*!
 * \file p_shapemodel.h
 *
 * \brief Planetary library - shape models for planetary body surfaces.
 *
 * Provides a pure-C, SPICE-free implementation of three planetary shape
 * models, matching the ISIS3 ShapeModel class hierarchy.
 *
 * Models
 * ------
 *  P_SHAPE_ELLIPSOID — Triaxial ellipsoid (a, b, c radii).
 *                      Closed-form ray intersection and analytic normal.
 *                      Used for the reference body shape under photometric
 *                      correction and for planets without a DEM.
 *
 *  P_SHAPE_DEM       — DEM/raster-backed shape.
 *                      A callback supplies the DEM radius at any (lat, lon);
 *                      ray intersection refines iteratively from the
 *                      ellipsoid seed.  Normal is estimated from four
 *                      nearest-neighbour DEM samples.
 *
 *  P_SHAPE_PLANE     — z = 0 plane (ring plane for Saturn, Uranus, etc.).
 *                      Trivial ray–plane intersection; normal is (0, 0, ±1).
 *
 * Coordinate conventions
 * -----------------------
 *  All XYZ positions are in body-fixed km (same as ISIS3 / SPICE).
 *  Latitude is planetocentric degrees (−90 to +90), positive north.
 *  Longitude is 0–360° east (matching ISIS3 positive-east convention).
 *
 * Row-processing with OpenMP
 * --------------------------
 *  p_shape_apply_row() evaluates incidence, emission, phase, and local radius
 *  for every pixel in a raster row, parallelised with OpenMP.
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 *         Algorithm sources: ISIS3 ShapeModel / EllipsoidShape (USGS Astrogeology)
 */

#ifndef GRASS_P_SHAPEMODEL_H
#define GRASS_P_SHAPEMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Model types                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    P_SHAPE_ELLIPSOID = 0,
    P_SHAPE_DEM       = 1,
    P_SHAPE_PLANE     = 2
} PShapeType;

/* ------------------------------------------------------------------ */
/* DEM radius callback                                                  */
/* ------------------------------------------------------------------ */

/*!
 * \brief Callback that returns the DEM radius at a given lat/lon.
 *
 * The caller implements this to perform bilinear interpolation in a
 * GRASS raster (or any other DEM source).
 *
 * \param lat_deg  planetocentric latitude in degrees  [−90, +90]
 * \param lon_deg  east longitude in degrees           [0, 360]
 * \param userdata opaque pointer passed to p_shape_dem()
 * \return  radius in kilometres; return ≤ 0 to signal "no data"
 */
typedef double (*PShapeRadiusFn)(double lat_deg, double lon_deg, void *userdata);

/* ------------------------------------------------------------------ */
/* Per-pixel photometric geometry result                                */
/* ------------------------------------------------------------------ */

/*!
 * \brief Output of p_shape_apply_row() for one pixel.
 *
 * All angles are in degrees.  An invalid geometry (ray misses body,
 * DEM "no data") is signalled by setting all fields to NaN.
 */
typedef struct {
    double incidence;   /*!< incidence angle  [deg]  (sun → surface)       */
    double emission;    /*!< emission angle   [deg]  (observer → surface)   */
    double phase;       /*!< phase angle      [deg]  (sun–surface–observer) */
    double local_radius;/*!< DEM/ellipsoid radius at intersection [km]       */
    double lat;         /*!< planetocentric latitude  [deg]                  */
    double lon;         /*!< east longitude           [deg]                  */
} PShapeResult;

/* ------------------------------------------------------------------ */
/* Opaque model handle                                                  */
/* ------------------------------------------------------------------ */

typedef struct PShapeModel PShapeModel;

/* ================================================================== */
/* Construction                                                         */
/* ================================================================== */

/*!
 * \brief Create a triaxial ellipsoid shape model.
 *
 * \param a_km  equatorial radius a (semi-major, km)
 * \param b_km  equatorial radius b (semi-intermediate, km)
 * \param c_km  polar radius c      (semi-minor, km)
 * \return heap-allocated PShapeModel*, or NULL on invalid parameters
 */
PShapeModel *p_shape_ellipsoid(double a_km, double b_km, double c_km);

/*!
 * \brief Create a sphere (special case of ellipsoid, a == b == c).
 */
PShapeModel *p_shape_sphere(double radius_km);

/*!
 * \brief Create a DEM-backed shape model.
 *
 * The ellipsoid radii (a, b, c) provide the initial ray–surface seed and
 * the reference for the iterative DEM refinement.
 *
 * \param fn        DEM radius callback (must not be NULL)
 * \param userdata  passed through to every call of fn
 * \param a_km      reference ellipsoid a-radius  [km]
 * \param b_km      reference ellipsoid b-radius  [km]
 * \param c_km      reference ellipsoid c-radius  [km]
 * \param dem_scale pixel scale for normal estimation [deg/sample]
 *                  (use 0 to let the library default to 1/1024°)
 * \return heap-allocated PShapeModel*, or NULL on invalid parameters
 */
PShapeModel *p_shape_dem(PShapeRadiusFn fn, void *userdata,
                          double a_km, double b_km, double c_km,
                          double dem_scale_deg);

/*!
 * \brief Create a z = 0 plane shape (ring plane).
 *
 * The plane is infinite and passes through the body centre.  The normal
 * direction is (0, 0, +1) when the observer is north of the plane,
 * (0, 0, −1) when south.
 *
 * \return heap-allocated PShapeModel*, or NULL on error
 */
PShapeModel *p_shape_plane(void);

/* ================================================================== */
/* Core geometric operations                                            */
/* ================================================================== */

/*!
 * \brief Find the ray–surface intersection point.
 *
 * \param m          shape model
 * \param obs[3]     observer body-fixed position [km]
 * \param dir[3]     unit look direction (normalised by caller)
 * \param pt_out[3]  output: intersection point in body-fixed km
 * \return 1 if intersection found, 0 if ray misses the body
 */
int p_shape_intersect_ray(const PShapeModel *m,
                           const double obs[3],
                           const double dir[3],
                           double pt_out[3]);

/*!
 * \brief Compute the outward surface normal at a body-fixed point.
 *
 * For the ellipsoid, this is the analytic gradient normalised to unit length.
 * For the DEM, this is estimated from four neighbour samples (finite-difference).
 * For the plane, this is always (0, 0, ±1).
 *
 * \param m           shape model
 * \param pt[3]       surface point in body-fixed km
 * \param normal[3]   output: unit outward normal vector
 */
void p_shape_normal(const PShapeModel *m,
                     const double pt[3],
                     double normal[3]);

/*!
 * \brief Get the surface radius at a given lat/lon [km].
 *
 * Ellipsoid: closed-form formula.
 * DEM: calls the registered callback.
 * Plane: returns 0 (undefined concept).
 */
double p_shape_local_radius_km(const PShapeModel *m,
                                double lat_deg, double lon_deg);

/* ================================================================== */
/* Angle calculations (geometry only, no shape model needed)            */
/* ================================================================== */

/*!
 * \brief Incidence angle [deg] at surface point given surface normal and sun position.
 *
 * \param pt[3]     surface point (body-fixed km)
 * \param normal[3] unit outward normal
 * \param sun[3]    sun body-fixed position (km)
 * \return incidence angle in degrees [0, 180]
 */
double p_shape_incidence_angle(const double pt[3],
                                const double normal[3],
                                const double sun[3]);

/*!
 * \brief Emission angle [deg] at surface point given surface normal and observer position.
 */
double p_shape_emission_angle(const double pt[3],
                               const double normal[3],
                               const double obs[3]);

/*!
 * \brief Phase angle [deg] between illuminator and observer as seen from surface point.
 */
double p_shape_phase_angle(const double pt[3],
                            const double obs[3],
                            const double sun[3]);

/* ================================================================== */
/* Coordinate conversions                                               */
/* ================================================================== */

/*!
 * \brief Convert body-fixed XYZ [km] to planetocentric lat/lon/radius.
 *
 * \param pt[3]      body-fixed position
 * \param lat_deg    output: latitude  [−90, +90]
 * \param lon_deg    output: longitude [0, 360)
 * \param radius_km  output: distance from body centre
 */
void p_shape_xyz_to_latlon(const double pt[3],
                             double *lat_deg, double *lon_deg, double *radius_km);

/*!
 * \brief Convert planetocentric lat/lon/radius to body-fixed XYZ [km].
 */
void p_shape_latlon_to_xyz(double lat_deg, double lon_deg, double radius_km,
                             double pt[3]);

/* ================================================================== */
/* Row-processing with OpenMP                                           */
/* ================================================================== */

/*!
 * \brief Compute photometric geometry for every pixel in a raster row.
 *
 * For each sample s in [0, nsamples):
 *   1. Intersect the ray (obs → dir[s]) with the shape model.
 *   2. Compute the surface normal at the intersection point.
 *   3. Compute incidence, emission, phase angles and local radius.
 *   4. Store results in out[s].
 *
 * Pixels where the ray misses the body have all result fields set to NaN.
 * Parallelised over samples with OpenMP.
 *
 * \param m          shape model
 * \param nsamples   number of pixels in this row
 * \param obs        observer body-fixed position [km] (same for all samples)
 * \param dirs       look-direction unit vectors, length 3*nsamples (dirs[3*s]…)
 * \param sun        sun body-fixed position [km] (same for all samples)
 * \param out        output PShapeResult array, length nsamples (caller-allocated)
 */
void p_shape_apply_row(const PShapeModel *m,
                        int nsamples,
                        const double obs[3],
                        const double *dirs,
                        const double sun[3],
                        PShapeResult *out);

/* ================================================================== */
/* Utilities                                                            */
/* ================================================================== */

/*! \brief Return a human-readable model name. */
const char *p_shape_name(const PShapeModel *m);

/*! \brief Free a PShapeModel created by any p_shape_*() constructor. */
void p_shape_free(PShapeModel *m);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_SHAPEMODEL_H */
