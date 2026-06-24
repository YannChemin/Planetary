/*!
 * \file p_spice.h
 *
 * \brief Planetary library - NAIF SPICE wrapper for GRASS.
 *
 * Provides a thin, error-safe C wrapper around the NAIF CSPICE toolkit
 * (N0067, /home/yann/dev/cspice).  All functions return 0 on success and
 * -1 on CSPICE error, calling G_warning() with the CSPICE short message.
 *
 * Thread safety
 * -------------
 * CSPICE itself is NOT re-entrant: it uses global kernel pools and global
 * error state.  All p_spice_* functions must therefore be called from a
 * single thread (or under an external mutex).  p_spice_angles_row() keeps
 * SPICE calls outside the OpenMP loop; only the angle arithmetic (pure C
 * dot-products) is parallelised.
 *
 * Angle conventions
 * -----------------
 * All angles returned are in **degrees** to match GRASS/ISIS3 conventions.
 * CSPICE returns angles in radians; conversion is applied internally.
 *
 * Usage workflow
 * --------------
 * 1. p_spice_init()        — set CSPICE error policy to RETURN (not ABORT)
 * 2. p_spice_load(kernel)  — furnsh_c() for each kernel file
 * 3. p_spice_str2et()      — convert image start/stop time to ET
 * 4. p_spice_pos() / p_spice_state() — spacecraft + sun positions
 * 5. p_spice_sincpt()      — per-pixel surface intercept  (for cam2map)
 * 6. p_spice_ilumin()      — photometric angles at a surface point
 * 7. p_spice_clear()       — unload all kernels when done
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#ifndef GRASS_P_SPICE_H
#define GRASS_P_SPICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Initialisation                                                       */
/* ================================================================== */

/*!
 * \brief Initialise CSPICE error handling.
 *
 * Must be called once before any other p_spice_* function.
 * Sets CSPICE error action to "RETURN" (functions return rather than abort)
 * and suppresses the default CSPICE error output (errors are forwarded to
 * G_warning() instead).
 */
void p_spice_init(void);

/* ================================================================== */
/* Kernel management                                                    */
/* ================================================================== */

/*!
 * \brief Load a SPICE kernel file (any type: LSK, SCLK, CK, SPK, IK, FK, PCK, DSK).
 *
 * Calls CSPICE furnsh_c().
 *
 * \param kernel_path  path to the kernel file
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_load(const char *kernel_path);

/*!
 * \brief Unload a single previously-loaded kernel file.
 *
 * Calls CSPICE unload_c().
 *
 * \param kernel_path  same path used in p_spice_load()
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_unload(const char *kernel_path);

/*!
 * \brief Unload all loaded kernels (calls CSPICE kclear_c()).
 */
void p_spice_clear(void);

/* ================================================================== */
/* Time conversion                                                      */
/* ================================================================== */

/*!
 * \brief Convert a UTC time string to ephemeris time (TDB seconds past J2000).
 *
 * Requires an LSK kernel to be loaded.
 *
 * \param utc_str  UTC time string, e.g. "2007-01-15T12:34:56.789"
 * \param et       output: ephemeris time [TDB seconds past J2000]
 * \return 0 on success, -1 on error (G_warning called)
 */
int p_spice_str2et(const char *utc_str, double *et);

/*!
 * \brief Convert ephemeris time to a UTC string.
 *
 * \param et      ephemeris time [TDB seconds past J2000]
 * \param format  CSPICE format string: "C" (calendar), "ISOC" (ISO 8601), "D" (DOY), "J" (Julian)
 * \param prec    decimal places for sub-seconds (0–14)
 * \param utc     output buffer
 * \param len     length of output buffer (minimum 30)
 * \return 0 on success, -1 on error
 */
int p_spice_et2utc(double et, const char *format, int prec,
                    char *utc, int len);

/* ================================================================== */
/* Body lookup                                                          */
/* ================================================================== */

/*!
 * \brief Look up the NAIF integer ID for a named solar-system body.
 *
 * \param name     body name (case-insensitive), e.g. "MARS", "499", "MRO"
 * \param naif_id  output: NAIF integer code
 * \return 0 on success, -1 if name not found
 */
int p_spice_name2id(const char *name, int *naif_id);

/*!
 * \brief Get the three triaxial radii of a body in km.
 *
 * Reads the BODY<id>_RADII keyword from loaded PCK kernels.
 *
 * \param body    body name, e.g. "MARS"
 * \param radii   output [3]: equatorial-a, equatorial-b, polar-c [km]
 * \return 0 on success, -1 on error
 */
int p_spice_radii(const char *body, double radii[3]);

/*!
 * \brief Get an arbitrary body constant from loaded PCK kernels.
 *
 * Wraps CSPICE bodvrd_c().
 *
 * \param body   body name
 * \param item   keyword, e.g. "RADII", "GM", "POLE_RA"
 * \param maxn   maximum number of values to retrieve
 * \param dim    output: actual number of values returned
 * \param values output array (caller-allocated, length ≥ maxn)
 * \return 0 on success, -1 on error
 */
int p_spice_bodvrd(const char *body, const char *item,
                    int maxn, int *dim, double *values);

/*!
 * \brief Fetch a double-precision array variable from the kernel pool.
 *
 * Wraps CSPICE gdpool_c(). Generic kernel-pool lookup -- not specific to
 * any instrument. Used e.g. to read instrument-specific text-kernel (IK)
 * variables such as a camera model's per-band coefficient table
 * (CRISM's "INS-74017_CAMERA_COEFF") or a boresight/slit-direction
 * vector ("INS-74017_BORESIGHT", "INS-74017_SLIT_DIRECTION") that have
 * no dedicated CSPICE accessor of their own.
 *
 * \param varname  kernel pool variable name (case-sensitive, as defined
 *                 in the loaded kernel, e.g. a .ti instrument kernel)
 * \param start    index of the first component to return (usually 0)
 * \param room     maximum number of values to retrieve
 * \param n_found  output: actual number of values found (0 if the
 *                 variable doesn't exist in the pool)
 * \param values   output array (caller-allocated, length >= room)
 * \return 0 on success, -1 if not found or on CSPICE error
 */
int p_spice_gdpool_d(const char *varname, int start, int room,
                      int *n_found, double *values);

/* ================================================================== */
/* Ephemeris geometry                                                   */
/* ================================================================== */

/*!
 * \brief Get the position of target relative to observer in a given frame.
 *
 * Wraps CSPICE spkpos_c().
 *
 * \param target    target body name or ID string (e.g. "SUN", "499")
 * \param et        ephemeris time [TDB s past J2000]
 * \param frame     reference frame (e.g. "IAU_MARS", "J2000")
 * \param abcorr    aberration correction (e.g. "LT+S", "NONE")
 * \param observer  observer body name (e.g. "MRO")
 * \param pos       output: position vector [km] in frame
 * \param lt        output: one-way light time [s]
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_pos(const char *target, double et,
                 const char *frame, const char *abcorr,
                 const char *observer,
                 double pos[3], double *lt);

/*!
 * \brief Get the state (position + velocity) of target relative to observer.
 *
 * Wraps CSPICE spkezr_c().
 *
 * \param state  output: [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z] [km, km/s]
 * \param lt     output: one-way light time [s]
 * \return 0 on success, -1 on error
 */
int p_spice_state(const char *target, double et,
                   const char *frame, const char *abcorr,
                   const char *observer,
                   double state[6], double *lt);

/*!
 * \brief Get the rotation matrix from frame \a from to frame \a to at time \a et.
 *
 * Wraps CSPICE pxform_c().  The resulting 3×3 matrix rotates a vector
 * expressed in frame \a from into frame \a to:
 *   v_to = rotate × v_from
 *
 * \param from    source frame (e.g. "J2000")
 * \param to      destination frame (e.g. "IAU_MARS")
 * \param et      ephemeris time
 * \param rotate  output: 3×3 rotation matrix (row-major)
 * \return 0 on success, -1 on error
 */
int p_spice_pxform(const char *from, const char *to, double et,
                    double rotate[3][3]);

/* ================================================================== */
/* Surface intercept                                                    */
/* ================================================================== */

/*!
 * \brief Find the surface intercept of a ray with a target body.
 *
 * Wraps CSPICE sincpt_c().  Used by p.cam2map to project a camera pixel
 * onto the planetary surface.
 *
 * \param method    intercept computation method: "Ellipsoid" or "DSK/Unprioritized"
 * \param target    target body name (e.g. "MARS")
 * \param et        ephemeris time of observation
 * \param fixref    body-fixed frame name (e.g. "IAU_MARS")
 * \param abcorr    aberration correction (e.g. "LT+S")
 * \param observer  observer body name (e.g. "MRO")
 * \param dref      ray direction frame (often same as fixref or "J2000")
 * \param dvec      ray direction unit vector in dref frame
 * \param spoint    output: surface intercept in fixref [km]
 * \param trgepc    output: epoch of target point (TDB) -- NOT optional,
 *                  must point to a real double (passed straight through
 *                  to sincpt_c, which writes through it unconditionally;
 *                  passing NULL segfaults deep inside CSPICE)
 * \param srfvec    output: vector from observer to spoint [km] -- NOT
 *                  optional, must be a real double[3] buffer, same
 *                  reason as trgepc
 * \return 1 if intercept found, 0 if ray misses body, -1 on CSPICE error
 */
int p_spice_sincpt(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    const char *dref, const double dvec[3],
                    double spoint[3], double *trgepc,
                    double srfvec[3]);

/*!
 * \brief Sub-observer point: the point on target nearest the observer.
 *
 * Wraps CSPICE subpnt_c() with NEAR POINT/ELLIPSOID or
 * NADIR/DSK/UNPRIORITIZED computation method (selected via \a method,
 * same "Ellipsoid"/"DSK/Unprioritized" convention as p_spice_sincpt()/
 * p_spice_latsrf() elsewhere in this library -- this function maps that
 * to the longer subpnt_c method strings internally).
 *
 * \param method    "Ellipsoid" or "DSK/Unprioritized"
 * \param target    target body name
 * \param et        observer's epoch
 * \param fixref    body-fixed frame name
 * \param abcorr    aberration correction (e.g. "LT+S")
 * \param observer  observer body name
 * \param spoint    output: sub-observer point in fixref [km]
 * \param trgepc    output: target epoch -- NOT optional, see p_spice_sincpt()
 * \param srfvec    output: vector from observer to spoint [km] -- NOT
 *                  optional, see p_spice_sincpt()
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_subpnt(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    double spoint[3], double *trgepc,
                    double srfvec[3]);

/*!
 * \brief Sub-solar point: the point on target nearest the Sun, as seen
 * from the given observer.
 *
 * Wraps CSPICE subslr_c(). Same method-string convention as
 * p_spice_subpnt().
 *
 * \param method    "Ellipsoid" or "DSK/Unprioritized"
 * \param target    target body name
 * \param et        observer's epoch
 * \param fixref    body-fixed frame name
 * \param abcorr    aberration correction (e.g. "LT+S")
 * \param observer  observer body name (defines the light-time/aberration
 *                  correction direction, per CSPICE's own convention)
 * \param spoint    output: sub-solar point in fixref [km]
 * \param trgepc    output: target epoch -- NOT optional, see p_spice_sincpt()
 * \param srfvec    output: vector from observer to spoint [km] -- NOT
 *                  optional, see p_spice_sincpt()
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_subslr(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    double spoint[3], double *trgepc,
                    double srfvec[3]);

/*!
 * \brief Map a known body-fixed (lon, lat) to a real surface point.
 *
 * Wraps CSPICE latsrf_c(). Unlike p_spice_sincpt(), this needs no
 * observer or look-direction ray -- only a target body-fixed (lon, lat),
 * exactly the information p.phocube's SPICE mode (-s) already has for
 * each pixel of a georeferenced raster (no per-pixel camera model
 * required). With method="DSK/Unprioritized" and a DSK kernel loaded,
 * this returns the real (non-ellipsoid) shape's surface point at that
 * (lon, lat); with method="Ellipsoid" it reduces to the same ellipsoid
 * intercept p_shapemodel already computes directly, without CSPICE.
 *
 * \param method    "Ellipsoid" or "DSK/Unprioritized"
 * \param target    target body name (e.g. "MARS")
 * \param et        ephemeris time (shape is generally time-invariant,
 *                  but the CSPICE API requires it)
 * \param fixref    body-fixed frame name (e.g. "IAU_MARS")
 * \param lon_deg   planetocentric longitude, degrees
 * \param lat_deg   planetocentric latitude, degrees
 * \param spoint    output: body-fixed surface point [km]
 * \return 0 on success, -1 on CSPICE error or missing DSK data
 */
int p_spice_latsrf(const char *method, const char *target, double et,
                    const char *fixref, double lon_deg, double lat_deg,
                    double spoint[3]);

/* ================================================================== */
/* Illumination geometry                                                */
/* ================================================================== */

/*!
 * \brief Compute illumination angles at a surface point.
 *
 * Wraps CSPICE ilumin_c().  Returns phase, incidence (solar), and emission
 * angles in **degrees** at a given body-fixed surface point.
 *
 * \param method        "Ellipsoid" or "DSK/Unprioritized"
 * \param target        target body name
 * \param et            ephemeris time
 * \param fixref        body-fixed reference frame
 * \param abcorr        aberration correction (e.g. "LT+S")
 * \param observer      observer body name
 * \param spoint        surface point in fixref [km]
 * \param phase_deg     output: phase angle [degrees]
 * \param incidence_deg output: solar incidence angle [degrees]
 * \param emission_deg  output: emission angle [degrees]
 * \return 0 on success, -1 on CSPICE error
 */
int p_spice_ilumin(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    const double spoint[3],
                    double *phase_deg,
                    double *incidence_deg,
                    double *emission_deg);

/* ================================================================== */
/* Row-level geometry (OpenMP over angle arithmetic, serial SPICE)     */
/* ================================================================== */

/*!
 * \brief Photometric geometry result for one pixel.
 */
typedef struct {
    double incidence_deg;  /*!< solar incidence angle [degrees]  */
    double emission_deg;   /*!< emission angle [degrees]          */
    double phase_deg;      /*!< phase angle [degrees]             */
    double lat_deg;        /*!< body-fixed latitude [degrees]     */
    double lon_deg;        /*!< body-fixed longitude [degrees]    */
    double radius_km;      /*!< surface radius from body centre   */
} PSpiceGeoResult;

/*!
 * \brief Compute per-pixel photometric geometry for a full raster row.
 *
 * The spacecraft position \a sc_bf and sun position \a sun_bf are evaluated
 * once (at the row mid-time \a et) and held constant across the row — valid
 * for pushbroom line exposures where ET variation is <1 ms per row.
 *
 * SPICE calls (sincpt_c / ilumin_c) are serialised inside this function
 * because CSPICE is not thread-safe.  The dot-product / arccos arithmetic
 * for angles is parallelised with OpenMP.
 *
 * \param method     intercept method: "Ellipsoid" or "DSK/Unprioritized"
 * \param target     target body name (e.g. "MARS")
 * \param et         mid-row ephemeris time
 * \param fixref     body-fixed reference frame (e.g. "IAU_MARS")
 * \param abcorr     aberration correction (e.g. "LT+S")
 * \param observer   observer body name (e.g. "MRO")
 * \param dref       look-direction frame (e.g. "J2000" or fixref)
 * \param nsamples   number of pixels in row
 * \param dirs       look-direction unit vectors [3 × nsamples], row-major
 * \param out        output PSpiceGeoResult per pixel (caller-allocated)
 * \return 0 on success, -1 on CSPICE error during initialisation
 */
int p_spice_geo_row(const char *method,
                     const char *target, double et,
                     const char *fixref, const char *abcorr,
                     const char *observer, const char *dref,
                     int nsamples, const double *dirs,
                     PSpiceGeoResult *out);

/* ================================================================== */
/* Error query                                                          */
/* ================================================================== */

/*!
 * \brief Return the CSPICE short error message from the most recent failure.
 *
 * Fills \a msg (caller-allocated, at least 256 bytes) and resets CSPICE
 * error state.  Returns 1 if there was a pending error, 0 otherwise.
 */
int p_spice_errmsg(char *msg, int len);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_SPICE_H */
