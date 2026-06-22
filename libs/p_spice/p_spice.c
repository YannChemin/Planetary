/*!
 * \file p_spice.c
 *
 * \brief Planetary library - NAIF CSPICE wrapper (implementation).
 *
 * Links against /home/yann/dev/cspice/lib/cspice.a (NAIF Toolkit N0067).
 * CSPICE is NOT re-entrant; all calls are serialised by the caller or via
 * the mutex in p_spice_geo_row().
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_spice.h"

/* ---- CSPICE headers ---- */
#include "SpiceUsr.h"
#include "SpiceZpr.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_SPICE_STANDALONE
#  include <stdarg.h>
static void *G_malloc(size_t n) { return malloc(n); }
static void  G_free(void *p)    { free(p); }
static void  G_warning(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "WARNING: "); vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
}
#  define _(s) (s)
#else
#  include <grass/gis.h>
#  include <grass/glocale.h>
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define RAD2DEG (180.0 / M_PI)

/* Short-message buffer size used throughout. */
#define ERRMSG_LEN 512

/* ================================================================== */
/* Internal: CSPICE error check → G_warning                           */
/* ================================================================== */

/*
 * If CSPICE failed, extract the short message, emit G_warning, reset
 * error state, and return -1.  Otherwise return 0.
 */
static int cspice_check(const char *fn)
{
    if (!failed_c()) return 0;

    char short_msg[ERRMSG_LEN] = "unknown";
    char long_msg[ERRMSG_LEN]  = "";
    getmsg_c("SHORT", sizeof(short_msg), short_msg);
    getmsg_c("LONG",  sizeof(long_msg),  long_msg);
    reset_c();

    if (long_msg[0])
        G_warning(_("p_spice %s: %s — %s"), fn, short_msg, long_msg);
    else
        G_warning(_("p_spice %s: %s"), fn, short_msg);

    return -1;
}

/* ================================================================== */
/* Initialisation                                                       */
/* ================================================================== */

void p_spice_init(void)
{
    /* Tell CSPICE to return on error instead of aborting the process. */
    erract_c("SET", 0, "RETURN");
    /* Suppress CSPICE's own error output — we forward to G_warning(). */
    errdev_c("SET", 0, "NULL");
    errprt_c("SET", 0, "NONE");
    /* Clear any pre-existing error state. */
    reset_c();
}

/* ================================================================== */
/* Kernel management                                                    */
/* ================================================================== */

int p_spice_load(const char *kernel_path)
{
    if (!kernel_path) { G_warning(_("p_spice_load: NULL path")); return -1; }
    furnsh_c(kernel_path);
    return cspice_check("load");
}

int p_spice_unload(const char *kernel_path)
{
    if (!kernel_path) return -1;
    unload_c(kernel_path);
    return cspice_check("unload");
}

void p_spice_clear(void)
{
    kclear_c();
    reset_c();
}

/* ================================================================== */
/* Time conversion                                                      */
/* ================================================================== */

int p_spice_str2et(const char *utc_str, double *et)
{
    if (!utc_str || !et) return -1;
    str2et_c(utc_str, et);
    return cspice_check("str2et");
}

int p_spice_et2utc(double et, const char *format, int prec,
                    char *utc, int len)
{
    if (!utc || len < 4) return -1;
    const char *fmt = format ? format : "ISOC";
    et2utc_c(et, fmt, (SpiceInt)prec, (SpiceInt)len, utc);
    return cspice_check("et2utc");
}

/* ================================================================== */
/* Body lookup                                                          */
/* ================================================================== */

int p_spice_name2id(const char *name, int *naif_id)
{
    if (!name || !naif_id) return -1;
    SpiceInt    code;
    SpiceBoolean found;
    bodn2c_c(name, &code, &found);
    if (cspice_check("name2id") < 0) return -1;
    if (!found) {
        G_warning(_("p_spice_name2id: body '%s' not found in loaded kernels"), name);
        return -1;
    }
    *naif_id = (int)code;
    return 0;
}

int p_spice_radii(const char *body, double radii[3])
{
    if (!body || !radii) return -1;
    SpiceInt dim;
    bodvrd_c(body, "RADII", 3, &dim, radii);
    if (cspice_check("radii") < 0) return -1;
    if (dim < 3) {
        G_warning(_("p_spice_radii: RADII for '%s' has only %d values (expected 3)"),
                   body, (int)dim);
        return -1;
    }
    return 0;
}

int p_spice_bodvrd(const char *body, const char *item,
                    int maxn, int *dim, double *values)
{
    if (!body || !item || !values || maxn < 1) return -1;
    SpiceInt sdim;
    bodvrd_c(body, item, (SpiceInt)maxn, &sdim, values);
    if (cspice_check("bodvrd") < 0) return -1;
    if (dim) *dim = (int)sdim;
    return 0;
}

int p_spice_gdpool_d(const char *varname, int start, int room,
                      int *n_found, double *values)
{
    if (!varname || !values || room < 1) return -1;
    SpiceInt n;
    SpiceBoolean found;
    gdpool_c(varname, (SpiceInt)start, (SpiceInt)room, &n, values, &found);
    if (cspice_check("gdpool") < 0) return -1;
    if (!found) {
        if (n_found) *n_found = 0;
        return -1;
    }
    if (n_found) *n_found = (int)n;
    return 0;
}

/* ================================================================== */
/* Ephemeris geometry                                                   */
/* ================================================================== */

int p_spice_pos(const char *target, double et,
                 const char *frame, const char *abcorr,
                 const char *observer,
                 double pos[3], double *lt)
{
    if (!target || !frame || !abcorr || !observer || !pos) return -1;
    double _lt;
    spkpos_c(target, (SpiceDouble)et, frame, abcorr, observer,
             pos, &_lt);
    if (cspice_check("pos") < 0) return -1;
    if (lt) *lt = _lt;
    return 0;
}

int p_spice_state(const char *target, double et,
                   const char *frame, const char *abcorr,
                   const char *observer,
                   double state[6], double *lt)
{
    if (!target || !frame || !abcorr || !observer || !state) return -1;
    double _lt;
    spkezr_c(target, (SpiceDouble)et, frame, abcorr, observer,
             state, &_lt);
    if (cspice_check("state") < 0) return -1;
    if (lt) *lt = _lt;
    return 0;
}

int p_spice_pxform(const char *from, const char *to, double et,
                    double rotate[3][3])
{
    if (!from || !to || !rotate) return -1;
    pxform_c(from, to, (SpiceDouble)et, rotate);
    return cspice_check("pxform");
}

/* ================================================================== */
/* Surface intercept                                                    */
/* ================================================================== */

int p_spice_sincpt(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    const char *dref, const double dvec[3],
                    double spoint[3], double *trgepc,
                    double srfvec[3])
{
    if (!method || !target || !fixref || !abcorr ||
        !observer || !dref || !dvec || !spoint) return -1;

    SpiceDouble _trgepc;
    SpiceBoolean found;

    sincpt_c(method, target, (SpiceDouble)et, fixref, abcorr,
             observer, dref,
             (ConstSpiceDouble *)dvec,
             spoint, &_trgepc,
             (SpiceDouble *)srfvec, &found);

    if (cspice_check("sincpt") < 0) return -1;
    if (trgepc) *trgepc = _trgepc;
    return found ? 1 : 0;
}

/* ================================================================== */
/* Lat/lon -> body-fixed surface point (no ray-casting/observer)        */
/* ================================================================== */

int p_spice_latsrf(const char *method, const char *target, double et,
                    const char *fixref, double lon_deg, double lat_deg,
                    double spoint[3])
{
    if (!method || !target || !fixref || !spoint) return -1;

    SpiceDouble lonlat[1][2];
    SpiceDouble srfpts[1][3];

    lonlat[0][0] = lon_deg * rpd_c();
    lonlat[0][1] = lat_deg * rpd_c();

    latsrf_c(method, target, (SpiceDouble)et, fixref, 1, lonlat, srfpts);

    if (cspice_check("latsrf") < 0) return -1;
    spoint[0] = srfpts[0][0];
    spoint[1] = srfpts[0][1];
    spoint[2] = srfpts[0][2];
    return 0;
}

/* ================================================================== */
/* Illumination geometry                                                */
/* ================================================================== */

int p_spice_ilumin(const char *method,
                    const char *target, double et,
                    const char *fixref, const char *abcorr,
                    const char *observer,
                    const double spoint[3],
                    double *phase_deg,
                    double *incidence_deg,
                    double *emission_deg)
{
    if (!method || !target || !fixref || !abcorr ||
        !observer || !spoint) return -1;

    SpiceDouble trgepc;
    SpiceDouble srfvec[3];
    SpiceDouble phase, solar, emissn;

    ilumin_c(method, target, (SpiceDouble)et, fixref, abcorr,
             observer,
             (ConstSpiceDouble *)spoint,
             &trgepc, (SpiceDouble *)srfvec,
             &phase, &solar, &emissn);

    if (cspice_check("ilumin") < 0) return -1;

    if (phase_deg)     *phase_deg     = phase  * RAD2DEG;
    if (incidence_deg) *incidence_deg = solar  * RAD2DEG;
    if (emission_deg)  *emission_deg  = emissn * RAD2DEG;
    return 0;
}

/* ================================================================== */
/* Row-level geometry                                                   */
/* ================================================================== */

/*
 * Strategy:
 *   1. Call p_spice_sincpt() for each pixel sequentially (CSPICE not re-entrant).
 *   2. Store spoint[s], found[s] for each sample.
 *   3. Parallelise ilumin_c calls — NO: ilumin_c is also not thread-safe.
 *
 * Therefore, both sincpt and ilumin loops are serial.  The benefit of
 * calling this function (vs. the caller doing it manually) is clean error
 * handling and a single allocation.
 *
 * A future version could use OpenMP with per-thread kernel contexts if NAIF
 * ever provides thread-safe kernels, but for N0067 serial is correct.
 */

int p_spice_geo_row(const char *method,
                     const char *target, double et,
                     const char *fixref, const char *abcorr,
                     const char *observer, const char *dref,
                     int nsamples, const double *dirs,
                     PSpiceGeoResult *out)
{
    if (!out || nsamples <= 0) return -1;

    /* Allocate per-pixel spoint storage. */
    double  *spoints = (double *)G_malloc((size_t)nsamples * 3 * sizeof(double));
    int     *found   = (int    *)G_malloc((size_t)nsamples * sizeof(int));

    /* --- Phase 1: serial sincpt for all pixels --- */
    for (int s = 0; s < nsamples; s++) {
        const double *dvec = dirs + 3*s;
        double spoint[3], srfvec[3], trgepc;

        int hit = p_spice_sincpt(method, target, et, fixref, abcorr,
                                  observer, dref, dvec,
                                  spoint, &trgepc, srfvec);
        found[s] = hit;
        if (hit == 1) {
            spoints[3*s+0] = spoint[0];
            spoints[3*s+1] = spoint[1];
            spoints[3*s+2] = spoint[2];
        } else {
            spoints[3*s+0] = spoints[3*s+1] = spoints[3*s+2] = 0.0;
        }
    }

    /* --- Phase 2: serial ilumin + lat/lon for pixels that hit --- */
    for (int s = 0; s < nsamples; s++) {
        if (found[s] != 1) {
            out[s].incidence_deg = out[s].emission_deg =
            out[s].phase_deg     = out[s].lat_deg      =
            out[s].lon_deg       = out[s].radius_km    = NAN;
            continue;
        }

        const double *sp = spoints + 3*s;
        double phase, incid, emissn;

        if (p_spice_ilumin(method, target, et, fixref, abcorr,
                            observer, sp,
                            &phase, &incid, &emissn) < 0) {
            out[s].incidence_deg = out[s].emission_deg =
            out[s].phase_deg     = NAN;
        } else {
            out[s].incidence_deg = incid;
            out[s].emission_deg  = emissn;
            out[s].phase_deg     = phase;
        }

        /* Convert body-fixed Cartesian → lat/lon/radius. */
        SpiceDouble radius, longitude, latitude;
        reclat_c((ConstSpiceDouble *)sp, &radius, &longitude, &latitude);
        out[s].radius_km = radius;
        out[s].lat_deg   = latitude  * RAD2DEG;
        out[s].lon_deg   = longitude * RAD2DEG;
        /* Ensure lon in [0, 360). */
        if (out[s].lon_deg < 0.0) out[s].lon_deg += 360.0;
    }

    G_free(spoints);
    G_free(found);
    return 0;
}

/* ================================================================== */
/* Error query                                                          */
/* ================================================================== */

int p_spice_errmsg(char *msg, int len)
{
    if (!failed_c()) return 0;
    getmsg_c("SHORT", (SpiceInt)len, msg);
    reset_c();
    return 1;
}
