/*!
 * \file p_projection_planet.h
 *
 * \brief Planetary library - projection types absent from standard PROJ/GRASS.
 *
 * Implements three ISIS3-derived map projections used for planetary science
 * that have no equivalent in the PROJ library used by standard GRASS GIS.
 *
 * Projections
 * -----------
 *  P_PROJ_RING_CYL
 *      Ring-plane cylindrical projection for Saturn, Jupiter, Uranus, Neptune
 *      ring imaging.  Maps (ring_radius, ring_longitude) to (x, y) with the
 *      origin at (center_radius, center_longitude).
 *        x = (ring_lon - center_lon) [rad] × center_radius
 *        y = center_radius - ring_radius
 *      Units of x and y match the units of center_radius (km or m).
 *
 *  P_PROJ_LUNAR_AZIMUTHAL_EA
 *      Lunar azimuthal equal-area projection for the Moon's near-side/far-side
 *      including the libration zone.  Extends the standard Lambert azimuthal
 *      equal-area projection with a libration scale factor.
 *
 *  P_PROJ_UPTURNED_TA
 *      Upturned-ellipsoid transverse azimuthal projection for irregular small
 *      bodies (asteroids, comets, Pluto) as published by Fleis et al. (2013).
 *      The "upturned" ellipsoid maps body oblateness via t = (b/a)^2.
 *
 * Coordinate conventions
 * ----------------------
 *  All angular inputs are in **degrees** unless noted.
 *  Longitude is positive-east (0–360° or −180–180°; callers normalise).
 *  Latitude is planetocentric.
 *  Linear outputs (x, y) are in the same units as the radius parameters.
 *
 * Row-processing (OpenMP)
 * -----------------------
 *  p_proj_planet_apply_row_fwd() and p_proj_planet_apply_row_inv() compute
 *  forward and inverse transforms for every pixel in a raster row using OpenMP.
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 *         Algorithms: ISIS3 RingCylindrical, LunarAzimuthalEqualArea,
 *         UpturnedEllipsoidTransverseAzimuthal (USGS Astrogeology, CC0-1.0)
 */

#ifndef GRASS_P_PROJECTION_PLANET_H
#define GRASS_P_PROJECTION_PLANET_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Projection type identifiers                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    P_PROJ_RING_CYL          = 0, /*!< Ring-plane cylindrical               */
    P_PROJ_LUNAR_AZIMUTHAL_EA = 1, /*!< Lunar azimuthal equal-area           */
    P_PROJ_UPTURNED_TA        = 2  /*!< Upturned-ellipsoid transverse azimuthal */
} PProjPlanetType;

/* ------------------------------------------------------------------ */
/* Parameter structs                                                    */
/* ------------------------------------------------------------------ */

/*!
 * \brief Parameters for the Ring-plane cylindrical projection.
 *
 * The projection is centred at (center_radius, center_lon_deg) and
 * uses center_radius as the scale factor.
 */
typedef struct {
    double center_radius;  /*!< Ring radius at projection centre [km or m] */
    double center_lon_deg; /*!< Ring longitude at centre [degrees, east-positive] */
    int    clockwise_lon;  /*!< 1 if ring longitudes increase clockwise, 0 otherwise */
} PProjRingCyl;

/*!
 * \brief Parameters for the Lunar azimuthal equal-area projection.
 */
typedef struct {
    double equatorial_radius; /*!< Body equatorial radius [same units as output x,y] */
    double max_libration_deg; /*!< Maximum libration angle [degrees], typically 8–10° */
} PProjLunarAzimuthalEA;

/*!
 * \brief Parameters for the Upturned-ellipsoid transverse azimuthal projection.
 */
typedef struct {
    double a;              /*!< Semi-major axis [same units as output x,y] */
    double b;              /*!< Semi-minor axis [same units as output x,y] */
    double center_lon_deg; /*!< Centre longitude [degrees, positive-east]   */
} PProjUpturnedTA;

/*!
 * \brief Union of all projection parameter sets.
 */
typedef union {
    PProjRingCyl          ring_cyl;
    PProjLunarAzimuthalEA lunar_ea;
    PProjUpturnedTA       upturned_ta;
} PProjPlanetParams;

/* ------------------------------------------------------------------ */
/* Default parameter macros                                             */
/* ------------------------------------------------------------------ */

/*! Saturn rings: centre at mid-B-ring (~105 000 km), lon=0. */
#define P_PROJ_RING_CYL_DEFAULTS \
    { .ring_cyl = { .center_radius=105000.0, .center_lon_deg=0.0, .clockwise_lon=0 } }

/*! Moon: R=1737.4 km, max libration=8°. */
#define P_PROJ_LUNAR_AZIMUTHAL_EA_DEFAULTS \
    { .lunar_ea = { .equatorial_radius=1737.4, .max_libration_deg=8.0 } }

/*! Generic sphere (a==b): unit sphere, centred at lon=0. */
#define P_PROJ_UPTURNED_TA_DEFAULTS \
    { .upturned_ta = { .a=1.0, .b=1.0, .center_lon_deg=0.0 } }

/* ------------------------------------------------------------------ */
/* Opaque projection handle                                             */
/* ------------------------------------------------------------------ */

typedef struct PProjPlanet PProjPlanet;

/* ================================================================== */
/* Construction                                                         */
/* ================================================================== */

/*!
 * \brief Create a planetary projection.
 *
 * \param type    projection identifier (P_PROJ_*)
 * \param params  parameters; NULL to use built-in defaults
 * \return heap-allocated PProjPlanet*, or NULL on invalid parameters
 */
PProjPlanet *p_proj_planet_create(PProjPlanetType type,
                                   const PProjPlanetParams *params);

/*!
 * \brief Free a PProjPlanet created by p_proj_planet_create().
 */
void p_proj_planet_free(PProjPlanet *p);

/* ================================================================== */
/* Forward transform: ground coordinates → map (x, y)                 */
/* ================================================================== */

/*!
 * \brief Compute the forward projection for a single point.
 *
 * For P_PROJ_RING_CYL:
 *   coord1 = ring radius  [same units as center_radius]
 *   coord2 = ring longitude [degrees]
 *
 * For P_PROJ_LUNAR_AZIMUTHAL_EA:
 *   coord1 = latitude  [degrees, planetocentric]
 *   coord2 = longitude [degrees, positive-east]
 *
 * For P_PROJ_UPTURNED_TA:
 *   coord1 = latitude  [degrees, planetocentric]
 *   coord2 = longitude [degrees, positive-east]
 *
 * \param p       open PProjPlanet
 * \param coord1  primary ground coordinate (radius or latitude)
 * \param coord2  secondary ground coordinate (ring longitude or longitude)
 * \param x       output map x coordinate
 * \param y       output map y coordinate
 * \return 1 on success, 0 on invalid geometry
 */
int p_proj_planet_fwd(const PProjPlanet *p,
                       double coord1, double coord2,
                       double *x, double *y);

/* ================================================================== */
/* Inverse transform: map (x, y) → ground coordinates                 */
/* ================================================================== */

/*!
 * \brief Compute the inverse projection for a single point.
 *
 * Returns ground coordinates in the same conventions as p_proj_planet_fwd().
 *
 * \param p       open PProjPlanet
 * \param x       map x coordinate
 * \param y       map y coordinate
 * \param coord1  output primary ground coordinate
 * \param coord2  output secondary ground coordinate
 * \return 1 on success, 0 on invalid geometry
 */
int p_proj_planet_inv(const PProjPlanet *p,
                       double x, double y,
                       double *coord1, double *coord2);

/* ================================================================== */
/* Row-processing with OpenMP                                           */
/* ================================================================== */

/*!
 * \brief Forward-project an entire raster row (OpenMP parallelised).
 *
 * For each sample s in [0, nsamples):
 *   if p_proj_planet_fwd succeeds: x_out[s]=x, y_out[s]=y
 *   else: x_out[s]=y_out[s]=NaN
 *
 * \param p        open PProjPlanet
 * \param nsamples number of pixels
 * \param coord1   primary ground coordinate array (length nsamples)
 * \param coord2   secondary ground coordinate array (length nsamples)
 * \param x_out    output x array (caller-allocated, length nsamples)
 * \param y_out    output y array (caller-allocated, length nsamples)
 */
void p_proj_planet_apply_row_fwd(const PProjPlanet *p,
                                  int nsamples,
                                  const double *coord1,
                                  const double *coord2,
                                  double *x_out,
                                  double *y_out);

/*!
 * \brief Inverse-project an entire raster row (OpenMP parallelised).
 *
 * For each sample s in [0, nsamples):
 *   if p_proj_planet_inv succeeds: coord1_out[s], coord2_out[s] set
 *   else: both set to NaN
 */
void p_proj_planet_apply_row_inv(const PProjPlanet *p,
                                  int nsamples,
                                  const double *x,
                                  const double *y,
                                  double *coord1_out,
                                  double *coord2_out);

/* ================================================================== */
/* Utilities                                                            */
/* ================================================================== */

/*! \brief Return a human-readable projection name. */
const char *p_proj_planet_name(const PProjPlanet *p);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_PROJECTION_PLANET_H */
