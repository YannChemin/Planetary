/****************************************************************************
 *
 * MODULE:       p.phocube
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Generate photometric and geometric backplane raster maps for
 *               a planetary image: per-pixel incidence, emission, phase angles,
 *               latitude, longitude, local radius, and pixel resolution.
 *
 *               Two operating modes:
 *               1. SPICE mode (-s): reads kernel paths from the input map's
 *                  history (written by p.spiceinit) and computes geometry via
 *                  NAIF CSPICE for every pixel.
 *               2. Flat-field mode (default): user supplies the solar and
 *                  observer unit vectors in body-fixed coordinates. The same
 *                  vectors are applied to all pixels. Used for quick testing
 *                  or for images without SPICE kernels.
 *
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/gprojects.h>
#include <grass/glocale.h>

/* p_shapemodel (compiled in) */
#include "../../libs/p_shapemodel/p_shapemodel.h"
/* p_spice (compiled in) -- SPICE mode (-s) only */
#include "../../libs/p_spice/p_spice.h"
/* p_meta (compiled in) -- camera mode (-c) instrument= auto-detection */
#include "../../libs/p_meta/p_meta.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

/* SPICE history metadata, written by p.spiceinit as one
 * "SPICE_<KEY>=<value>" line per call to Rast_append_history(). Kernel
 * paths may be semicolon-separated (multiple files per type); TARGET/
 * OBSERVER/TIME/LINE_RATE are single values. Last occurrence of a given
 * key wins (handles a raster having been re-spiceinit'd). LINE_RATE is
 * optional (seconds/row); when absent, -s mode uses a single mid-scene
 * epoch (TIME) for every row -- see p.phocube.md NOTES. */
#define SPICE_META_PREFIX "SPICE_"

typedef struct {
    char target[64];
    char observer[64];
    char time[64];
    double line_rate;  /* seconds per output row; 0 = single mid-scene epoch */
    int  have_target, have_observer, have_time, have_line_rate, have_dsk;
    int  n_kernels_loaded;
} SpiceHistoryInfo;

static void spice_load_paths(const char *value, int *n_loaded)
{
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s", value);
    char *tok = strtok(buf, ";");
    while (tok) {
        if (p_spice_load(tok) < 0)
            G_warning(_("SPICE: failed to load kernel '%s'"), tok);
        else
            (*n_loaded)++;
        tok = strtok(NULL, ";");
    }
}

/* Read every SPICE_* key written by p.spiceinit from the input map's
 * history, loading kernels as they're found. G_fatal_error()s if no
 * target/observer/time is found -- -s mode cannot proceed without them. */
static SpiceHistoryInfo read_spice_history(const char *mapname, const char *mapset)
{
    SpiceHistoryInfo info;
    memset(&info, 0, sizeof(info));

    struct History hist;
    if (Rast_read_history(mapname, mapset, &hist) < 0)
        G_fatal_error(_("No history metadata on <%s> -- run p.spiceinit first."),
                       mapname);

    int nlines = Rast_history_length(&hist);
    for (int i = 0; i < nlines; i++) {
        const char *line = Rast_history_line(&hist, i);
        if (strncmp(line, SPICE_META_PREFIX, strlen(SPICE_META_PREFIX)) != 0)
            continue;
        const char *kv = line + strlen(SPICE_META_PREFIX);
        const char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        char key[32];
        size_t keylen = (size_t)(eq - kv);
        if (keylen >= sizeof(key))
            continue;
        memcpy(key, kv, keylen);
        key[keylen] = '\0';
        const char *value = eq + 1;

        if (strcmp(key, "TARGET") == 0) {
            snprintf(info.target, sizeof(info.target), "%s", value);
            info.have_target = 1;
        }
        else if (strcmp(key, "OBSERVER") == 0) {
            snprintf(info.observer, sizeof(info.observer), "%s", value);
            info.have_observer = 1;
        }
        else if (strcmp(key, "TIME") == 0) {
            snprintf(info.time, sizeof(info.time), "%s", value);
            info.have_time = 1;
        }
        else if (strcmp(key, "LINE_RATE") == 0) {
            info.line_rate = atof(value);
            info.have_line_rate = 1;
        }
        else if (strcmp(key, "DSK") == 0) {
            int n_before = info.n_kernels_loaded;
            spice_load_paths(value, &info.n_kernels_loaded);
            if (info.n_kernels_loaded > n_before)
                info.have_dsk = 1;
        }
        else if (strcmp(key, "LSK") == 0 || strcmp(key, "SCLK") == 0 ||
                 strcmp(key, "CK") == 0 || strcmp(key, "SPK") == 0 ||
                 strcmp(key, "IK") == 0 || strcmp(key, "FK") == 0 ||
                 strcmp(key, "PCK") == 0) {
            spice_load_paths(value, &info.n_kernels_loaded);
        }
    }
    Rast_free_history(&hist);

    if (!info.have_target || !info.have_observer || !info.have_time)
        G_fatal_error(_("SPICE mode (-s) requires target=, observer= and "
                        "time= to have been attached to <%s> via "
                        "p.spiceinit (found: target=%s observer=%s time=%s)."),
                       mapname,
                       info.have_target ? info.target : "(missing)",
                       info.have_observer ? info.observer : "(missing)",
                       info.have_time ? info.time : "(missing)");
    if (info.n_kernels_loaded == 0)
        G_fatal_error(_("SPICE mode (-s) found no loadable kernels in <%s>'s "
                        "history -- run p.spiceinit with lsk=/spk=/etc first."),
                       mapname);

    return info;
}

static void uppercase_copy(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Camera mode (-c): pinhole instrument camera models                  */
/*                                                                      */
/* CRISM (VNIR detector -74017 / IR detector -74018) and Cassini ISS    */
/* (NAC -82360 / WAC -82361) are both modeled as a standard pinhole     */
/* focal-plane map, the same way ISIS3's own camera classes do. The     */
/* boresight/pixel-pitch/focal-length/distortion values are NOT in the */
/* public NAIF IK -- they live in ISIS3's separately-distributed        */
/* instrument addendum kernels (fetched from the ISIS3 AWS data        */
/* mirror, see p.spice.find's kernels=...,iak), which must be attached  */
/* alongside the regular IK via p.spiceinit's ik= option:               */
/*   CRISM_VNIR/CRISM_IR : crismAddendum001.ti                          */
/*   ISS_NAC             : IssNAAddendum005.ti                          */
/*   ISS_WAC             : IssWAAddendum005.ti                          */
/*                                                                      */
/* CRISM is 1-D (cross-track sample only, boresight_line/K1 stay 0) --  */
/* per-line pointing comes from the gimbal CK over time instead. ISS is */
/* a real 2-D framing camera with a single static boresight per frame   */
/* and genuine radial lens distortion (K1, ISIS3's own                  */
/* RadialDistortionMap convention: ux=dx*(1+K1*r2), uy=dy*(1+K1*r2)).    */
/* ISS's focal length also varies per filter-pair (FOCAL_LENGTH is not  */
/* one IK keyword but dozens, keyed "INS<id>_<F1>_<F2>_FOCAL_LENGTH" --  */
/* filter1_opt/filter2_opt come from -c's filter1=/filter2= or, when     */
/* unset, the raster's own p_meta "filter_name" sidecar field (set by   */
/* p.in.archive's OPUS ISS import), falling back to the IAK's own       */
/* DEFAULT_FOCAL_LENGTH (its comment: "not being used... but was left   */
/* in") with a warning if neither is available -- a real, IAK-          */
/* documented fallback, unlike CRISM's discredited CAMERA_COEFF guess.  */
/* ISS also requires the IAK's own custom frame (e.g.                   */
/* CASSINI_ISS_NAC_USGS, a 180-degree fix for a real, documented gap in */
/* NAIF's own cas_v*.tf), not the bare NAIF frame -- resolvable once the */
/* IAK is furnsh'd via ik=, same mechanism as its other keywords.       */
/*                                                                      */
/* MEX OMEGA SWIR-C/SWIR-L are a third, genuinely different shape: not  */
/* a pinhole focal-plane map at all, but a whiskbroom scanning mirror.  */
/* No IAK exists for OMEGA (none on the ISIS3 AWS mirror) -- the model  */
/* comes entirely from the real public NAIF/ESA IK (MEX_OMEGA_V03.TI's  */
/* "OMEGA Pixels Geometry" section): each pixel's pointing is the       */
/* "central" pixel vector (boresight, (0,0,1) in the detector's own     */
/* frame) rotated about the detector frame's +Y axis by                 */
/* offset_angle = (dn_position - MIRROR_CENTER_POSITION) * MIRROR_SLOPE */
/* degrees, where dn_position is the REAL per-sample scanning-mirror     */
/* position (DN) recorded in the cube's own QUBE band-suffix sideplane   */
/* (see p.in.pds3's suffix_band= and OMEGA_HK.TXT) -- mirror_dn= here   */
/* points at that already-imported raster (one row per line, one value */
/* per sample within it). MIRROR_CENTER_POSITION/MIRROR_SLOPE are read   */
/* from the IK under the shared SWIR id (-41420), not the per-channel   */
/* SWIR-C/SWIR-L id, since both channels share one physical mirror.     */
/* OMEGA_VNIR (synced-acquisition only -- see the comment in            */
/* load_pinhole_camera_model()) reuses this identical mirror_dn/        */
/* offset_angle formula, just rotated out of its own MEX_OMEGA_VNIR     */
/* detector frame instead of SWIR's -- confirmed against a real cube    */
/* whose VIS channel shares SWIR's 64-sample line width exactly.        */
/*                                                                      */
/* The real FK (MEX_V16.TF) centers MEX_OMEGA_SWIR_C/_SWIR_L/_VNIR's     */
/* frame on the MEX_OMEGA instrument body (-41400), which has no SPK    */
/* ephemeris of its own (a fixed-mount instrument id, not a tracked     */
/* body) -- sincpt's dref handling needs that center body's state       */
/* regardless of aberration correction. Since all three are plain       */
/* fixed-angle TKFRAMEs relative to MEX_SPACECRAFT (SWIR-L and VNIR via  */
/* SWIR-C), the fix is to pre-rotate dvec into MEX_SPACECRAFT ourselves  */
/* (a one-time, time-independent pxform -- TK frames have no light-time */
/* dependency) and pass dref="MEX_SPACECRAFT" to sincpt instead, since   */
/* -41 (the spacecraft) has real ephemeris throughout.                  */
/*                                                                      */
/* Cassini VIMS_IR/VIMS_VIS are a fourth shape: a real 2-axis angular    */
/* scanning model (IR: true 2-axis scanning mirror, "whiskbroom";       */
/* VIS: 1-axis mirror sweeping a CCD line, "pushbroom" -- but ISIS3's    */
/* own VimsGroundMap::LookDirection() uses the identical formula for     */
/* both, since both axes have a real, documented angular pixel pitch    */
/* either way). Ported directly from ISIS3's VimsGroundMap.cpp (no       */
/* public IK or IAK has this -- the public IK only gives the overall    */
/* FOV envelope, and the IAK (vimsAddendum*.ti) only fixes a real,       */
/* documented VIMS_IR/VIMS_V NAIF ID swap bug, confirmed by reading the  */
/* IAK's own comment -- it does not add a boresight/pixel-pitch model).  */
/* Unlike CRISM/ISS/OMEGA, this model outputs a unit look vector         */
/* directly in spherical (theta, phi) terms, with no intermediate        */
/* focal-plane mm step:                                                  */
/*   x = sample + camSampOffset;  y = line + camLineOffset               */
/*   theta = pi/2 - (y - yBore) * yPixSize                               */
/*   phi   = -pi/2 + (x - xBore) * xPixSize                              */
/*   v = ( sin(theta)*cos(phi), cos(theta), -sin(theta)*sin(phi) )       */
/* xPixSize/yPixSize/xBore/yBore and the integer camSampOffset/           */
/* camLineOffset (note: truncating integer division on purpose, exactly */
/* matching ISIS3's own int arithmetic) depend on channel (IR/VIS) and   */
/* SamplingMode (NORMAL/HI-RES) -- both real per-cube values from the    */
/* PDS3 label's Instrument group (SAMPLING_MODE_ID, X_OFFSET, Z_OFFSET,  */
/* SWATH_WIDTH, SWATH_LENGTH), not from any kernel. cam->frame is the     */
/* plain NAIF frame (CASSINI_VIMS_IR/_V) -- both have real ephemeris      */
/* (FRAME_-8237{0,1}_CENTER = -82, the orbiter), no pxform workaround     */
/* needed (unlike OMEGA's -41400 instrument-body issue).                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int    naif_id;
    char   frame[64];
    double boresight_sample;
    double boresight_line;
    double pixel_pitch;
    double focal_length;
    double k1;
    int    is_framing;  /* 1: line is a real focal-plane offset (ISS).
                          * 0: line is time, not focal-plane geometry
                          * (CRISM) -- per-line pointing instead comes
                          * from the gimbal CK; dy must stay 0. */
    int    is_omega;       /* 1: OMEGA whiskbroom scanning-mirror model
                              instead of a pinhole focal-plane map. */
    double mirror_center;  /* INS-41420_MIRROR_CENTER_POSITION (DN)    */
    double mirror_slope;   /* INS-41420_MIRROR_SLOPE (deg/DN)          */
    double omega_rot[3][3]; /* fixed rotation: detector frame -> MEX_SPACECRAFT */
    int    is_vims;         /* 1: VIMS 2-axis angular scan model (see
                                comment block above LookDirection-style
                                fields below) instead of a pinhole map. */
    double vims_x_pixsize;  /* rad/pixel, cross-track (sample) axis */
    double vims_y_pixsize;  /* rad/pixel, down-track (line) axis */
    double vims_x_bore;     /* boresight sample, in the full 64x64 FOV grid */
    double vims_y_bore;     /* boresight line, in the full 64x64 FOV grid */
    int    vims_samp_offset; /* this cube's swath sample offset into the
                                 full 64x64 grid (XOffset-derived) */
    int    vims_line_offset; /* this cube's swath line offset (ZOffset-derived) */
} PinholeCameraModel;

/* Optional manual overrides for camera-model parameters that don't live
 * in any SPICE kernel -- they come from the PDS3 label's Instrument group
 * (VIMS) or sidecar p_meta (ISS). Read from -c's own CLI options first,
 * else the input raster's planetary.json, in load_pinhole_camera_model(). */
typedef struct {
    const char *filter1;        /* ISS_NAC/ISS_WAC */
    const char *filter2;        /* ISS_NAC/ISS_WAC */
    const char *sampling_mode;  /* VIMS_IR/VIMS_VIS: "NORMAL" or "HI-RES" */
    const char *x_offset;       /* VIMS_IR/VIMS_VIS: real label X_OFFSET */
    const char *z_offset;       /* VIMS_IR/VIMS_VIS: real label Z_OFFSET */
    const char *swath_width;    /* VIMS_IR/VIMS_VIS: real label SWATH_WIDTH */
    const char *swath_length;   /* VIMS_IR/VIMS_VIS: real label SWATH_LENGTH */
} CameraOverrides;

static void load_pinhole_camera_model(const char *instrument,
                                       const char *input_map,
                                       const CameraOverrides *ov,
                                       int image_cols, int image_rows,
                                       PinholeCameraModel *cam)
{
    memset(cam, 0, sizeof(*cam));

    int is_iss = 0;

    if (strcmp(instrument, "OMEGA_SWIR_C") == 0) {
        cam->naif_id = -41421;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_SWIR_C");
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "OMEGA_SWIR_L") == 0) {
        cam->naif_id = -41422;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_SWIR_L");
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "OMEGA_VNIR") == 0) {
        /* Synced-acquisition VNIR (the only kind p.in.pds3/p.in.archive
         * currently import -- confirmed against a real cube,
         * ORB0100_0.QUB: CHANNEL_ID=(IRC,IRL,VIS), CORE_ITEMS sample=64,
         * identical to SWIR-C/SWIR-L's sample count, not VNIR's native
         * 384/128-pixel pushbroom width) shares the SWIR mirror's real
         * per-line/per-sample telemetry one-for-one: at each mirror
         * step the same physical sweep that builds one SWIR sample also
         * yields one VNIR sample, so the identical
         * offset_angle=(dn-MIRROR_CENTER_POSITION)*MIRROR_SLOPE formula
         * applies, just rotated out of MEX_OMEGA_VNIR's own detector
         * frame (a fixed TKFRAME relative to MEX_OMEGA_SWIR_C, per
         * MEX_V16.TF) instead of SWIR's. The native-resolution,
         * unsynced 128-pixel VNIR pushbroom mode (MEX_OMEGA_V03.TI's
         * INS-41410_PIXEL_DN calibration table) is a different,
         * currently non-importable product type -- not implemented. */
        cam->naif_id = -41410;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_VNIR");
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "CRISM_VNIR") == 0) {
        cam->naif_id = -74017;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_VNIR");
    }
    else if (strcmp(instrument, "CRISM_IR") == 0) {
        cam->naif_id = -74018;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_IR");
    }
    else if (strcmp(instrument, "ISS_NAC") == 0) {
        cam->naif_id = -82360;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_NAC_USGS");
        is_iss = 1;
        cam->is_framing = 1;
    }
    else if (strcmp(instrument, "ISS_WAC") == 0) {
        cam->naif_id = -82361;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_WAC_USGS");
        is_iss = 1;
        cam->is_framing = 1;
    }
    else if (strcmp(instrument, "VIMS_IR") == 0 ||
             strcmp(instrument, "VIMS_VIS") == 0) {
        int is_ir = (strcmp(instrument, "VIMS_IR") == 0);
        cam->naif_id = is_ir ? -82371 : -82370;
        snprintf(cam->frame, sizeof(cam->frame),
                 is_ir ? "CASSINI_VIMS_IR" : "CASSINI_VIMS_V");
        cam->is_vims = 1;

        char samp_mode[16] = "";
        if (ov->sampling_mode && ov->sampling_mode[0])
            snprintf(samp_mode, sizeof(samp_mode), "%s", ov->sampling_mode);
        if (!samp_mode[0]) {
            char buf[16];
            const char *field = is_ir ? "sampling_mode_ir" : "sampling_mode_vis";
            if (p_meta_read_string_field(input_map, "raster", field,
                                          buf, sizeof(buf)) == 0)
                snprintf(samp_mode, sizeof(samp_mode), "%s", buf);
        }
        if (!samp_mode[0])
            G_fatal_error(_("Camera mode (-c): instrument=%s needs the real "
                            "SamplingMode (NORMAL or HI-RES) -- pass "
                            "sampling_mode= or import via p.in.archive's "
                            "vims= (which writes it to planetary.json)."),
                           instrument);

        long x_offset = 0, z_offset = 0, swath_width = 0, swath_length = 0;
        const char *vims_fields[4]   = { "x_offset", "z_offset",
                                          "swath_width", "swath_length" };
        const char *vims_overrides[4] = { ov->x_offset, ov->z_offset,
                                           ov->swath_width, ov->swath_length };
        long *vims_out[4] = { &x_offset, &z_offset, &swath_width, &swath_length };
        for (int i = 0; i < 4; i++) {
            if (vims_overrides[i] && vims_overrides[i][0]) {
                *vims_out[i] = atol(vims_overrides[i]);
                continue;
            }
            char buf[16];
            if (p_meta_read_string_field(input_map, "raster", vims_fields[i],
                                          buf, sizeof(buf)) == 0)
                *vims_out[i] = atol(buf);
            else
                G_fatal_error(_("Camera mode (-c): instrument=%s needs the "
                                "real label field '%s' -- pass %s= or "
                                "import via p.in.archive's vims= (which "
                                "writes it to planetary.json)."),
                               instrument, vims_fields[i], vims_fields[i]);
        }

        int hires = (strcasecmp(samp_mode, "HI-RES") == 0);
        int camSampOffset, camLineOffset;
        double xPixSize, yPixSize, xBore, yBore;

        if (!is_ir) {
            if (!hires) {
                xPixSize = yPixSize = 0.00051;
                xBore = yBore = 31;
                camSampOffset = (int)x_offset - 1;
                camLineOffset = (int)z_offset - 1;
            }
            else {
                xPixSize = yPixSize = 0.00051 / 3.0;
                xBore = yBore = 94;
                camSampOffset = (3 * ((int)x_offset + (int)swath_width / 2)) -
                                (int)swath_width / 2;
                camLineOffset = (3 * ((int)z_offset + (int)swath_length / 2)) -
                                (int)swath_length / 2;
            }
        }
        else {
            if (!hires) {
                xPixSize = yPixSize = 0.000495;
                xBore = yBore = 31;
                camSampOffset = (int)x_offset - 1;
                camLineOffset = (int)z_offset - 1;
            }
            else {
                xPixSize = 0.000495 / 2.0;
                yPixSize = 0.000495;
                xBore = 62.5;
                yBore = 31;
                camSampOffset = 2 * (((int)x_offset - 1) +
                                     (((int)swath_width - 1) / 4));
                camLineOffset = (int)z_offset - 1;
            }
        }

        cam->vims_x_pixsize = xPixSize;
        cam->vims_y_pixsize = yPixSize;
        cam->vims_x_bore = xBore;
        cam->vims_y_bore = yBore;
        cam->vims_samp_offset = camSampOffset;
        cam->vims_line_offset = camLineOffset;
        return;
    }
    else
        G_fatal_error(_("Camera mode (-c): unsupported instrument='%s' "
                        "(supports CRISM_VNIR, CRISM_IR, ISS_NAC, ISS_WAC, "
                        "OMEGA_SWIR_C, OMEGA_SWIR_L, OMEGA_VNIR, VIMS_IR, "
                        "VIMS_VIS)."),
                       instrument);

    char varname[80];
    int n;

    if (cam->is_omega) {
        /* Mirror parameters live under the shared SWIR id (-41420), not
         * the per-channel SWIR-C/SWIR-L id -- one physical mirror serves
         * both InSb arrays. */
        if (p_spice_gdpool_d("INS-41420_MIRROR_CENTER_POSITION", 0, 1, &n,
                              &cam->mirror_center) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): could not read "
                            "INS-41420_MIRROR_CENTER_POSITION from the "
                            "loaded IK (MEX_OMEGA_V03.TI)."));
        if (p_spice_gdpool_d("INS-41420_MIRROR_SLOPE", 0, 1, &n,
                              &cam->mirror_slope) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): could not read "
                            "INS-41420_MIRROR_SLOPE from the loaded IK "
                            "(MEX_OMEGA_V03.TI)."));

        /* Pre-rotate into MEX_SPACECRAFT (see the comment block above
         * this struct) -- a fixed TKFRAME chain, so et is irrelevant. */
        if (p_spice_pxform(cam->frame, "MEX_SPACECRAFT", 0.0, cam->omega_rot) < 0)
            G_fatal_error(_("Camera mode (-c): pxform('%s' -> 'MEX_SPACECRAFT') "
                            "failed -- is the real MEX frame kernel "
                            "(MEX_V16.TF) attached via p.spiceinit's fk=?"),
                           cam->frame);
        snprintf(cam->frame, sizeof(cam->frame), "MEX_SPACECRAFT");
        return;
    }

    const char *iak_hint = is_iss
        ? "the IssNAAddendum/IssWAAddendum instrument addendum kernel"
        : "the CRISM instrument addendum kernel (crismAddendum001.ti)";

    snprintf(varname, sizeof(varname), "INS%d_PIXEL_PITCH", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->pixel_pitch) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                        "IK -- has %s been attached via p.spiceinit's ik= "
                        "option, in addition to the regular IK?"),
                       varname, iak_hint);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_SAMPLE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_sample) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_LINE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_line) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    if (!is_iss) {
        snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                           varname);
        return;
    }

    /* ISS: BORESIGHT_SAMPLE/LINE and PIXEL_PITCH in the IK/IAK are given in
     * the full-resolution (1x1) detector frame (1024x1024), but real
     * images are often acquired SUMMED/binned (e.g. INSTRUMENT_MODE_ID=
     * "SUM2" -> 512x512) -- using the raw IK values directly against a
     * summed image's own pixel coordinates silently shifts every ray off
     * by the summing factor. Detect it by comparing the IK's own full-
     * frame INS<id>_PIXEL_SAMPLES/PIXEL_LINES to this image's actual
     * dimensions (more robust than parsing INSTRUMENT_MODE_ID text). */
    double pixel_samples_full = 0.0, pixel_lines_full = 0.0;
    snprintf(varname, sizeof(varname), "INS%d_PIXEL_SAMPLES", cam->naif_id);
    int have_ps = (p_spice_gdpool_d(varname, 0, 1, &n, &pixel_samples_full) == 0 && n == 1);
    snprintf(varname, sizeof(varname), "INS%d_PIXEL_LINES", cam->naif_id);
    int have_pl = (p_spice_gdpool_d(varname, 0, 1, &n, &pixel_lines_full) == 0 && n == 1);

    double summing_s = 1.0, summing_l = 1.0;
    if (have_ps && image_cols > 0)
        summing_s = pixel_samples_full / (double)image_cols;
    if (have_pl && image_rows > 0)
        summing_l = pixel_lines_full / (double)image_rows;

    if (summing_s != 1.0 || summing_l != 1.0) {
        G_message(_("Camera mode (-c): detected summing/binning factor "
                    "%.0fx%.0f (IK full frame %.0fx%.0f vs image %dx%d) -- "
                    "adjusting boresight and pixel pitch accordingly."),
                   summing_s, summing_l, pixel_samples_full, pixel_lines_full,
                   image_cols, image_rows);
        cam->boresight_sample = (cam->boresight_sample - 1.0) / summing_s + 1.0;
        cam->boresight_line   = (cam->boresight_line   - 1.0) / summing_l + 1.0;
    }
    cam->pixel_pitch *= (summing_s + summing_l) / 2.0;

    /* ISS: real radial distortion, always present. */
    snprintf(varname, sizeof(varname), "INS%d_K1", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->k1) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    /* ISS: focal length varies per filter pair -- resolve filter1/filter2
     * from -c's own options, else the raster's p_meta sidecar, else fall
     * back to the IAK's own DEFAULT_FOCAL_LENGTH (documented fallback). */
    char f1[32] = "", f2[32] = "";
    if (ov->filter1) snprintf(f1, sizeof(f1), "%s", ov->filter1);
    if (ov->filter2) snprintf(f2, sizeof(f2), "%s", ov->filter2);

    if (!f1[0] || !f2[0]) {
        char filt_buf[64];
        if (p_meta_read_string_field(input_map, "raster", "filter_name",
                                      filt_buf, sizeof(filt_buf)) == 0) {
            char *slash = strchr(filt_buf, '/');
            if (slash) {
                *slash = '\0';
                snprintf(f1, sizeof(f1), "%s", filt_buf);
                snprintf(f2, sizeof(f2), "%s", slash + 1);
            }
        }
    }

    int got_focal = 0;
    if (f1[0] && f2[0]) {
        snprintf(varname, sizeof(varname), "INS%d_%s_%s_FOCAL_LENGTH",
                 cam->naif_id, f1, f2);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) == 0 && n == 1)
            got_focal = 1;
        else
            G_warning(_("Camera mode (-c): %s not found in the loaded IAK "
                        "(wrong filter order? real keys are label order, "
                        "e.g. CL1_CL2 not CL2_CL1) -- falling back to "
                        "DEFAULT_FOCAL_LENGTH."), varname);
    }
    if (!got_focal) {
        snprintf(varname, sizeof(varname), "INS%d_DEFAULT_FOCAL_LENGTH", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): no usable FOCAL_LENGTH found "
                            "(no filter1=/filter2=, no 'filter_name' in the "
                            "raster's planetary.json, and %s missing too)."),
                           varname);
        if (!f1[0] || !f2[0])
            G_warning(_("Camera mode (-c): no filter1=/filter2= given and no "
                        "'filter_name' in this raster's planetary.json -- "
                        "using %s (the IAK's own comment: 'not being used... "
                        "but was left in' -- real images always have a known "
                        "filter pair; pass filter1=/filter2= for an accurate "
                        "result)."), varname);
    }
}

/* ------------------------------------------------------------------ */
/* Which backplane outputs to generate                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int incidence;
    int emission;
    int phase;
    int lat;
    int lon;
    int local_radius;
    int resolution;
} BackplaneSet;

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_prefix;
    struct Option  *opt_target, *opt_a, *opt_b, *opt_c;
    struct Option  *opt_sun_x, *opt_sun_y, *opt_sun_z;
    struct Option  *opt_obs_x, *opt_obs_y, *opt_obs_z;
    struct Option  *opt_instrument, *opt_filter1, *opt_filter2, *opt_mirror_dn;
    struct Option  *opt_vims_sampling_mode, *opt_vims_x_offset, *opt_vims_z_offset;
    struct Option  *opt_vims_swath_width, *opt_vims_swath_length;
    struct Flag    *flag_inc, *flag_emi, *flag_pha, *flag_lat;
    struct Flag    *flag_lon, *flag_rad, *flag_res, *flag_all;
    struct Flag    *flag_spice, *flag_camera;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Photometric Analysis"));
    G_add_keyword(_("photometry"));
    G_add_keyword(_("geometry"));
    G_add_keyword(_("backplane"));
    module->label       = _("Generate photometric and geometric backplane rasters.");
    module->description = _("Creates per-pixel maps of incidence angle, emission angle, "
                             "phase angle, latitude, longitude, local radius, and pixel "
                             "resolution for a planetary image. Geometry is computed from "
                             "an ellipsoid shape model using either user-supplied solar/"
                             "observer vectors (flat-field mode) or SPICE kernels (-s flag). "
                             "Output maps are named prefix_incidence, prefix_emission, etc.");

    opt_input = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->description = _("Input planetary raster map");

    opt_prefix = G_define_option();
    opt_prefix->key         = "output";
    opt_prefix->type        = TYPE_STRING;
    opt_prefix->required    = YES;
    opt_prefix->description = _("Prefix for output backplane raster names");

    opt_target = G_define_option();
    opt_target->key         = "target";
    opt_target->type        = TYPE_STRING;
    opt_target->required    = NO;
    opt_target->answer      = "MARS";
    opt_target->description = _("Target body name (for ellipsoid radii lookup)");

    opt_a = G_define_option();
    opt_a->key         = "a_radius";
    opt_a->type        = TYPE_DOUBLE;
    opt_a->required    = NO;
    opt_a->answer      = "3396.19";
    opt_a->description = _("Ellipsoid semi-major (equatorial) radius [km]");

    opt_b = G_define_option();
    opt_b->key         = "b_radius";
    opt_b->type        = TYPE_DOUBLE;
    opt_b->required    = NO;
    opt_b->answer      = "3396.19";
    opt_b->description = _("Ellipsoid semi-intermediate radius [km] (use a_radius for sphere)");

    opt_c = G_define_option();
    opt_c->key         = "c_radius";
    opt_c->type        = TYPE_DOUBLE;
    opt_c->required    = NO;
    opt_c->answer      = "3376.20";
    opt_c->description = _("Ellipsoid polar (semi-minor) radius [km]");

    /* Flat-field sun vector (unit vector in body-fixed frame, pointing FROM body TO sun) */
    opt_sun_x = G_define_option();
    opt_sun_x->key         = "sun_x";
    opt_sun_x->type        = TYPE_DOUBLE;
    opt_sun_x->required    = NO;
    opt_sun_x->answer      = "1.0";
    opt_sun_x->description = _("Sun direction body-fixed X (unit vector, flat-field mode)");

    opt_sun_y = G_define_option();
    opt_sun_y->key = "sun_y"; opt_sun_y->type = TYPE_DOUBLE;
    opt_sun_y->required = NO; opt_sun_y->answer = "0.0";
    opt_sun_y->description = _("Sun direction body-fixed Y");

    opt_sun_z = G_define_option();
    opt_sun_z->key = "sun_z"; opt_sun_z->type = TYPE_DOUBLE;
    opt_sun_z->required = NO; opt_sun_z->answer = "0.0";
    opt_sun_z->description = _("Sun direction body-fixed Z");

    /* Flat-field observer position (body-fixed, km from centre) */
    opt_obs_x = G_define_option();
    opt_obs_x->key         = "obs_x";
    opt_obs_x->type        = TYPE_DOUBLE;
    opt_obs_x->required    = NO;
    opt_obs_x->answer      = "0.0";
    opt_obs_x->description = _("Observer position body-fixed X [km] (flat-field mode)");

    opt_obs_y = G_define_option();
    opt_obs_y->key = "obs_y"; opt_obs_y->type = TYPE_DOUBLE;
    opt_obs_y->required = NO; opt_obs_y->answer = "0.0";
    opt_obs_y->description = _("Observer position body-fixed Y [km]");

    opt_obs_z = G_define_option();
    opt_obs_z->key = "obs_z"; opt_obs_z->type = TYPE_DOUBLE;
    opt_obs_z->required = NO; opt_obs_z->answer = "10000.0";
    opt_obs_z->description = _("Observer position body-fixed Z [km] (default: above north pole)");

    /* Output selection flags */
    flag_inc = G_define_flag(); flag_inc->key = 'i';
    flag_inc->description = _("Generate incidence angle map");
    flag_emi = G_define_flag(); flag_emi->key = 'e';
    flag_emi->description = _("Generate emission angle map");
    flag_pha = G_define_flag(); flag_pha->key = 'p';
    flag_pha->description = _("Generate phase angle map");
    flag_lat = G_define_flag(); flag_lat->key = 't';
    flag_lat->description = _("Generate latitude map");
    flag_lon = G_define_flag(); flag_lon->key = 'n';
    flag_lon->description = _("Generate longitude map");
    flag_rad = G_define_flag(); flag_rad->key = 'r';
    flag_rad->description = _("Generate local radius map");
    flag_res = G_define_flag(); flag_res->key = 'x';
    flag_res->description = _("Generate pixel resolution map [km/pixel]");
    flag_all = G_define_flag(); flag_all->key = 'a';
    flag_all->description = _("Generate all backplane maps");

    flag_spice = G_define_flag(); flag_spice->key = 's';
    flag_spice->label = _("SPICE mode: real per-pixel ephemeris geometry");
    flag_spice->description = _("Reads target/observer/time and kernel paths from the "
                                 "input map's history (written by p.spiceinit) and computes "
                                 "incidence/emission/phase from real NAIF CSPICE ephemeris "
                                 "(p_spice_ilumin) instead of the flat-field sun_x/y/z, "
                                 "obs_x/y/z vectors. Requires the input map's region to be "
                                 "a real geographic or projected CRS (not an un-georeferenced "
                                 "pixel/line grid, e.g. as produced by 'p.in.pds3 -g' for raw "
                                 "pushbroom cubes) -- see NOTES.");

    flag_camera = G_define_flag(); flag_camera->key = 'c';
    flag_camera->label = _("Camera mode: real per-pixel boresight ray (CRISM only, v1)");
    flag_camera->description = _("Builds a real per-pixel look-direction ray from the "
                                  "instrument's boresight + per-band camera-model "
                                  "coefficients (read from the IK attached via p.spiceinit) "
                                  "and intersects it with the target surface (p_spice_sincpt) "
                                  "instead of assuming the region already gives a known "
                                  "(lon, lat) per pixel. For raw, un-projected pushbroom "
                                  "cubes (e.g. CRISM TRDR/EDR) where -s cannot be used. "
                                  "Requires instrument=. See NOTES.");

    opt_instrument = G_define_option();
    opt_instrument->key         = "instrument";
    opt_instrument->type        = TYPE_STRING;
    opt_instrument->required    = NO;
    opt_instrument->options     = "CRISM_VNIR,CRISM_IR,ISS_NAC,ISS_WAC,"
                                   "OMEGA_SWIR_C,OMEGA_SWIR_L,OMEGA_VNIR,"
                                   "VIMS_IR,VIMS_VIS";
    opt_instrument->description = _("Instrument camera model to use with -c");

    opt_filter1 = G_define_option();
    opt_filter1->key         = "filter1";
    opt_filter1->type        = TYPE_STRING;
    opt_filter1->required    = NO;
    opt_filter1->description = _("ISS_NAC/ISS_WAC: first filter wheel position "
                                  "(e.g. CL1) -- focal length varies per filter "
                                  "pair; default: read from the raster's own "
                                  "planetary.json 'filter_name', set by "
                                  "p.in.archive's OPUS ISS import");

    opt_filter2 = G_define_option();
    opt_filter2->key         = "filter2";
    opt_filter2->type        = TYPE_STRING;
    opt_filter2->required    = NO;
    opt_filter2->description = _("ISS_NAC/ISS_WAC: second filter wheel position "
                                  "(e.g. CL2)");

    opt_mirror_dn = G_define_standard_option(G_OPT_R_INPUT);
    opt_mirror_dn->key         = "mirror_dn";
    opt_mirror_dn->required    = NO;
    opt_mirror_dn->label       = _("OMEGA_SWIR_C/OMEGA_SWIR_L/OMEGA_VNIR: "
                                    "scanning mirror position raster");
    opt_mirror_dn->description = _("Per-sample scanning mirror position (DN), "
                                    "one value per sample/line -- import via "
                                    "'p.in.pds3 suffix_band=1' on the same QUBE "
                                    "(see OMEGA_HK.TXT). Required for "
                                    "OMEGA_SWIR_C/OMEGA_SWIR_L.");

    opt_vims_sampling_mode = G_define_option();
    opt_vims_sampling_mode->key         = "sampling_mode";
    opt_vims_sampling_mode->type        = TYPE_STRING;
    opt_vims_sampling_mode->required    = NO;
    opt_vims_sampling_mode->options     = "NORMAL,HI-RES";
    opt_vims_sampling_mode->description = _("VIMS_IR/VIMS_VIS: real label "
                                  "SamplingMode -- default: read from the "
                                  "raster's own planetary.json "
                                  "'sampling_mode_ir'/'sampling_mode_vis', "
                                  "set by p.in.archive's vims= import");

    opt_vims_x_offset = G_define_option();
    opt_vims_x_offset->key         = "x_offset";
    opt_vims_x_offset->type        = TYPE_INTEGER;
    opt_vims_x_offset->required    = NO;
    opt_vims_x_offset->description = _("VIMS_IR/VIMS_VIS: real label "
                                  "X_OFFSET -- default: read from "
                                  "planetary.json");

    opt_vims_z_offset = G_define_option();
    opt_vims_z_offset->key         = "z_offset";
    opt_vims_z_offset->type        = TYPE_INTEGER;
    opt_vims_z_offset->required    = NO;
    opt_vims_z_offset->description = _("VIMS_IR/VIMS_VIS: real label "
                                  "Z_OFFSET -- default: read from "
                                  "planetary.json");

    opt_vims_swath_width = G_define_option();
    opt_vims_swath_width->key         = "swath_width";
    opt_vims_swath_width->type        = TYPE_INTEGER;
    opt_vims_swath_width->required    = NO;
    opt_vims_swath_width->description = _("VIMS_IR/VIMS_VIS: real label "
                                  "SWATH_WIDTH -- default: read from "
                                  "planetary.json");

    opt_vims_swath_length = G_define_option();
    opt_vims_swath_length->key         = "swath_length";
    opt_vims_swath_length->type        = TYPE_INTEGER;
    opt_vims_swath_length->required    = NO;
    opt_vims_swath_length->description = _("VIMS_IR/VIMS_VIS: real label "
                                  "SWATH_LENGTH -- default: read from "
                                  "planetary.json");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *input   = opt_input->answer;
    const char *prefix  = opt_prefix->answer;
    double a_km = atof(opt_a->answer);
    double b_km = atof(opt_b->answer);
    double c_km = atof(opt_c->answer);
    double sun[3]  = { atof(opt_sun_x->answer),
                       atof(opt_sun_y->answer),
                       atof(opt_sun_z->answer) };
    double obs[3]  = { atof(opt_obs_x->answer),
                       atof(opt_obs_y->answer),
                       atof(opt_obs_z->answer) };

    /* Normalise sun direction to unit vector */
    double sun_len = sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);
    if (sun_len > 0) { sun[0]/=sun_len; sun[1]/=sun_len; sun[2]/=sun_len; }
    /* Scale sun to "far away" distance for angle computation */
    double sun_dist = 1.5e8; /* ~1 AU in km */
    sun[0] *= sun_dist; sun[1] *= sun_dist; sun[2] *= sun_dist;

    BackplaneSet bp = {
        flag_all->answer || flag_inc->answer,
        flag_all->answer || flag_emi->answer,
        flag_all->answer || flag_pha->answer,
        flag_all->answer || flag_lat->answer,
        flag_all->answer || flag_lon->answer,
        flag_all->answer || flag_rad->answer,
        flag_all->answer || flag_res->answer
    };

    /* Default: generate all if none specified */
    if (!bp.incidence && !bp.emission && !bp.phase &&
        !bp.lat && !bp.lon && !bp.local_radius && !bp.resolution) {
        bp.incidence = bp.emission = bp.phase =
        bp.lat = bp.lon = bp.local_radius = bp.resolution = 1;
    }

    /* ---------------------------------------------------------------- */
    /* Validate input map exists                                         */
    /* ---------------------------------------------------------------- */
    const char *input_mapset = G_find_raster((char *)input, "");
    if (!input_mapset)
        G_fatal_error(_("Raster map <%s> not found"), input);

    /* ---------------------------------------------------------------- */
    /* SPICE mode (-s) setup: real target/observer/time/kernels from      */
    /* history, real ephemeris time, real body radii from the loaded PCK. */
    /* ---------------------------------------------------------------- */
    int spice_mode = flag_spice->answer;
    int camera_mode = flag_camera->answer;
    SpiceHistoryInfo spice_info;
    double et = 0.0;
    char fixref[80] = "";
    char target_upper[64] = "";

    if (spice_mode && camera_mode)
        G_fatal_error(_("-s and -c are mutually exclusive (two different "
                        "ways of getting a per-pixel surface point)."));
    if (camera_mode && !opt_instrument->answer) {
        /* Auto-detect from the sensor= field p.in.archive -s writes into
         * planetary.json for CRISM imports, so -c works without typing
         * instrument= by hand. Falls through to the fatal error below,
         * unchanged, for inputs with no/unrecognized metadata. */
        char sensor_buf[64];
        if (p_meta_read_string_field(input, "raster", "sensor", sensor_buf,
                                      sizeof(sensor_buf)) == 0) {
            if (strcmp(sensor_buf, "MRO_CRISM_VNIR") == 0)
                opt_instrument->answer = "CRISM_VNIR";
            else if (strcmp(sensor_buf, "MRO_CRISM_IR") == 0)
                opt_instrument->answer = "CRISM_IR";
            else if (strcmp(sensor_buf, "CASSINI_ISS_NAC") == 0)
                opt_instrument->answer = "ISS_NAC";
            else if (strcmp(sensor_buf, "CASSINI_ISS_WAC") == 0)
                opt_instrument->answer = "ISS_WAC";
        }
    }
    if (camera_mode && !opt_instrument->answer)
        G_fatal_error(_("-c requires instrument= (CRISM_VNIR, CRISM_IR, "
                        "ISS_NAC, ISS_WAC, OMEGA_SWIR_C, OMEGA_SWIR_L, "
                        "OMEGA_VNIR, VIMS_IR, or VIMS_VIS)."));

    int is_omega_instrument = camera_mode && opt_instrument->answer &&
        (strcmp(opt_instrument->answer, "OMEGA_SWIR_C") == 0 ||
         strcmp(opt_instrument->answer, "OMEGA_SWIR_L") == 0 ||
         strcmp(opt_instrument->answer, "OMEGA_VNIR") == 0);
    if (is_omega_instrument && !opt_mirror_dn->answer)
        G_fatal_error(_("instrument=%s requires mirror_dn= (the per-sample "
                        "scanning mirror position raster, imported via "
                        "'p.in.pds3 suffix_band=1')."), opt_instrument->answer);

    /* Projection handling for -s: a real geographic/projected CRS is
     * required (see NOTES) -- an un-georeferenced pixel/line grid
     * (PROJECTION_XY, e.g. p.in.pds3 -g output for raw pushbroom cubes)
     * cannot be safely interpreted as lat/lon and must fail loudly
     * rather than silently misinterpreting sample/line indices as
     * degrees (the bug this -s mode replaces). */
    int proj_type = 0;
    struct pj_info iproj, oproj, tproj;
    int use_gpj_transform = 0;

    if (spice_mode) {
        p_spice_init();
        spice_info = read_spice_history(input, input_mapset);
        if (p_spice_str2et(spice_info.time, &et) < 0)
            G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                            "time (is an LSK kernel attached?)."), spice_info.time);

        uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
        snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

        double radii[3];
        if (p_spice_radii(spice_info.target, radii) == 0) {
            a_km = radii[0]; b_km = radii[1]; c_km = radii[2];
            G_message(_("SPICE: using real %s radii from loaded PCK: "
                        "a=%.3f b=%.3f c=%.3f km"), target_upper, a_km, b_km, c_km);
        }
        else {
            G_warning(_("SPICE: no PCK radii found for '%s'; falling back to "
                        "a_radius=/b_radius=/c_radius= (%.3f/%.3f/%.3f km)."),
                       spice_info.target, a_km, b_km, c_km);
        }

        proj_type = G_projection();
        if (proj_type == PROJECTION_XY) {
            G_fatal_error(_("SPICE mode (-s) requires a real geographic or "
                            "projected CRS; the current location is an "
                            "un-georeferenced pixel/line grid (PROJECTION_XY, "
                            "e.g. as produced by 'p.in.pds3 -g' for raw "
                            "pushbroom cubes). Re-project to the body's real "
                            "footprint first, or use flat-field mode (no -s)."));
        }
        else if (proj_type != PROJECTION_LL) {
            struct Key_Value *pin = G_get_projinfo();
            struct Key_Value *uin = G_get_projunits();
            if (!pin || !uin)
                G_fatal_error(_("Cannot read projection info/units from the "
                                "current location."));
            if (pj_get_kv(&iproj, pin, uin) < 0)
                G_fatal_error(_("pj_get_kv failed."));
            G_free_key_value(pin);
            G_free_key_value(uin);
            oproj.pj = NULL;
            tproj.def = NULL;
            if (GPJ_init_transform(&iproj, &oproj, &tproj) < 0)
                G_fatal_error(_("GPJ_init_transform failed -- cannot convert "
                                "this location's CRS to geographic lon/lat."));
            use_gpj_transform = 1;
            G_message(_("SPICE: working CRS is projected; converting each "
                        "pixel to real lon/lat via GPJ_transform()."));
        }
        else {
            G_message(_("SPICE: working location is already geographic "
                        "(PROJECTION_LL); using region east/north directly "
                        "as lon/lat."));
        }
    }

    /* ---------------------------------------------------------------- */
    /* Camera mode (-c) setup: real target/observer/time/kernels from     */
    /* history (same as -s), plus the CRISM instrument camera model.      */
    /* No projection/region-CRS handling needed -- row/col are just       */
    /* (line, sample) indices into the camera model, not coordinates.     */
    /* ---------------------------------------------------------------- */
    struct Cell_head region;
    G_get_window(&region);
    int nrows = region.rows;
    int ncols = region.cols;

    PinholeCameraModel cam;
    const char *camera_method = "Ellipsoid";

    if (camera_mode) {
        p_spice_init();
        spice_info = read_spice_history(input, input_mapset);
        if (p_spice_str2et(spice_info.time, &et) < 0)
            G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                            "time (is an LSK kernel attached?)."), spice_info.time);

        uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
        snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

        double radii[3];
        if (p_spice_radii(spice_info.target, radii) == 0) {
            a_km = radii[0]; b_km = radii[1]; c_km = radii[2];
            G_message(_("SPICE: using real %s radii from loaded PCK: "
                        "a=%.3f b=%.3f c=%.3f km"), target_upper, a_km, b_km, c_km);
        }
        else
            G_warning(_("SPICE: no PCK radii found for '%s'; falling back to "
                        "a_radius=/b_radius=/c_radius= (%.3f/%.3f/%.3f km)."),
                       spice_info.target, a_km, b_km, c_km);

        if (spice_info.have_dsk)
            camera_method = "DSK/Unprioritized";

        CameraOverrides cam_overrides = {
            .filter1 = opt_filter1->answer,
            .filter2 = opt_filter2->answer,
            .sampling_mode = opt_vims_sampling_mode->answer,
            .x_offset = opt_vims_x_offset->answer,
            .z_offset = opt_vims_z_offset->answer,
            .swath_width = opt_vims_swath_width->answer,
            .swath_length = opt_vims_swath_length->answer,
        };
        load_pinhole_camera_model(opt_instrument->answer, input,
                                   &cam_overrides, ncols, nrows, &cam);
        if (cam.is_omega)
            G_message(_("Camera mode: instrument=%s frame=%s "
                        "mirror_center=%.3f DN mirror_slope=%.7f deg/DN"),
                       opt_instrument->answer, cam.frame,
                       cam.mirror_center, cam.mirror_slope);
        else if (cam.is_vims)
            G_message(_("Camera mode: instrument=%s frame=%s "
                        "x_pixsize=%.6g rad y_pixsize=%.6g rad "
                        "bore=(%.1f,%.1f) samp_offset=%d line_offset=%d"),
                       opt_instrument->answer, cam.frame,
                       cam.vims_x_pixsize, cam.vims_y_pixsize,
                       cam.vims_x_bore, cam.vims_y_bore,
                       cam.vims_samp_offset, cam.vims_line_offset);
        else
            G_message(_("Camera mode: instrument=%s frame=%s focal_length=%.3f mm "
                        "pixel_pitch=%.3f mm boresight=(%.1f,%.1f) k1=%g"),
                       opt_instrument->answer, cam.frame, cam.focal_length,
                       cam.pixel_pitch, cam.boresight_sample, cam.boresight_line,
                       cam.k1);
    }

    /* OMEGA: open the per-sample mirror-position raster (one row per
     * input line, same column count as the input cube). */
    int mirror_dn_fd = -1;
    DCELL *mirror_dn_row = NULL;
    if (camera_mode && cam.is_omega) {
        const char *mirror_dn_mapset = G_find_raster(opt_mirror_dn->answer, "");
        if (!mirror_dn_mapset)
            G_fatal_error(_("Raster map <%s> (mirror_dn=) not found"),
                          opt_mirror_dn->answer);
        mirror_dn_fd = Rast_open_old(opt_mirror_dn->answer, mirror_dn_mapset);
        mirror_dn_row = Rast_allocate_d_buf();
    }

    /* ---------------------------------------------------------------- */
    /* Build ellipsoid shape model                                       */
    /* ---------------------------------------------------------------- */
    PShapeModel *shape = p_shape_ellipsoid(a_km, b_km, c_km);
    if (!shape)
        G_fatal_error(_("Cannot create ellipsoid shape model (a=%g, b=%g, c=%g km)"),
                       a_km, b_km, c_km);

    /* ---------------------------------------------------------------- */
    /* Compute pixel coordinates → lat/lon                                */
    /* ---------------------------------------------------------------- */
    G_message(_("Computing backplanes for %d x %d pixels ..."), nrows, ncols);
    G_message(_("  Ellipsoid: a=%.3f  b=%.3f  c=%.3f km"), a_km, b_km, c_km);
    if (spice_mode || camera_mode) {
        G_message(_("  SPICE: target=%s observer=%s time=%s (et=%.3f)"),
                   target_upper, spice_info.observer, spice_info.time, et);
        if (spice_info.have_line_rate)
            G_message(_("  SPICE: line_rate=%.6f s/row -- per-row ephemeris "
                        "time, not a single mid-scene epoch"),
                       spice_info.line_rate);
    }
    else
        G_message(_("  Observer: (%.1f, %.1f, %.1f) km body-fixed"), obs[0], obs[1], obs[2]);

    /* ---------------------------------------------------------------- */
    /* Open output file descriptors                                       */
    /* ---------------------------------------------------------------- */
    char mapname[512];
    int fd_inc=-1, fd_emi=-1, fd_pha=-1;
    int fd_lat=-1, fd_lon=-1, fd_rad=-1, fd_res=-1;

#define OPEN_OUT(fd, suffix) \
    if (bp.suffix) { \
        snprintf(mapname, sizeof(mapname), "%s_" #suffix, prefix); \
        fd = Rast_open_new(mapname, DCELL_TYPE); \
    }

    OPEN_OUT(fd_inc, incidence);
    OPEN_OUT(fd_emi, emission);
    OPEN_OUT(fd_pha, phase);
    OPEN_OUT(fd_lat, lat);
    OPEN_OUT(fd_lon, lon);
    OPEN_OUT(fd_rad, local_radius);
    OPEN_OUT(fd_res, resolution);

    /* ---------------------------------------------------------------- */
    /* Allocate row buffers                                              */
    /* ---------------------------------------------------------------- */
    DCELL *row_inc = Rast_allocate_d_buf();
    DCELL *row_emi = Rast_allocate_d_buf();
    DCELL *row_pha = Rast_allocate_d_buf();
    DCELL *row_lat = Rast_allocate_d_buf();
    DCELL *row_lon = Rast_allocate_d_buf();
    DCELL *row_rad = Rast_allocate_d_buf();
    DCELL *row_res = Rast_allocate_d_buf();
    PShapeResult *results = (PShapeResult *)G_malloc((size_t)ncols * sizeof(PShapeResult));

    /* ---------------------------------------------------------------- */
    /* Per-row computation: for each pixel, ray-cast to ellipsoid        */
    /* ---------------------------------------------------------------- */
    double *dirs = (double *)G_malloc((size_t)ncols * 3 * sizeof(double));

    int spice_failed_pixels = 0;

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);

        if (spice_mode) {
            /* SPICE mode: each pixel's real lon/lat is already known from
             * the region + CRS (no ray-casting needed -- we're not solving
             * "where does this ray hit", we already know the point), so
             * call p_spice_ilumin() directly per pixel with that known
             * body-fixed surface point and the real ephemeris time.
             *
             * et_row: with line_rate= attached (real per-line cadence),
             * each row gets its own ephemeris time relative to the
             * mid-scene epoch instead of one constant et for the whole
             * scene -- real pushbroom/framing acquisitions take a real,
             * non-zero scan duration, so row 0 and row nrows-1 were not
             * actually acquired at the same instant. Without line_rate=
             * (have_line_rate == 0) this is a no-op (et_row == et). */
            double et_row = et;
            if (spice_info.have_line_rate)
                et_row = et + (row - (nrows - 1) / 2.0) * spice_info.line_rate;

            for (int col = 0; col < ncols; col++) {
                double east  = region.west  + (col + 0.5) * region.ew_res;
                double north = region.north - (row + 0.5) * region.ns_res;

                double lon_deg = east, lat_deg = north;
                if (use_gpj_transform) {
                    if (GPJ_transform(&iproj, &oproj, &tproj, PJ_FWD,
                                      &lon_deg, &lat_deg, NULL) < 0)
                        G_fatal_error(_("GPJ_transform failed at pixel "
                                        "(row=%d, col=%d)."), row, col);
                }
                /* else: PROJECTION_LL -- region east/north already are
                 * real lon/lat, no transform needed. */

                /* DSK kernel attached: use the real (non-ellipsoid) shape
                 * via latsrf -- still no ray-casting/camera model needed,
                 * since we already know this pixel's (lon, lat). Falls
                 * back to the ellipsoid for any (lon, lat) the DSK
                 * doesn't cover (latsrf returns < 0), rather than
                 * G_fatal_error -- a real DSK tile boundary is a normal
                 * condition, not a configuration error. */
                double r_km;
                double pt[3];
                const char *ilumin_method = "Ellipsoid";
                int used_dsk = 0;
                if (spice_info.have_dsk &&
                    p_spice_latsrf("DSK/Unprioritized", target_upper, et_row,
                                  fixref, lon_deg, lat_deg, pt) == 0) {
                    r_km = sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
                    ilumin_method = "DSK/Unprioritized";
                    used_dsk = 1;
                }
                if (!used_dsk) {
                    r_km = p_shape_local_radius_km(shape, lat_deg, lon_deg);
                    p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, pt);
                }

                double phase_deg, incidence_deg, emission_deg;
                int ok = (p_spice_ilumin(ilumin_method, target_upper, et_row, fixref,
                                         "LT+S", spice_info.observer, pt,
                                         &phase_deg, &incidence_deg,
                                         &emission_deg) == 0);

                if (!ok) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                }
                else {
                    if (bp.incidence) row_inc[col] = incidence_deg;
                    if (bp.emission)  row_emi[col] = emission_deg;
                    if (bp.phase)     row_pha[col] = phase_deg;
                }
                if (bp.lat) row_lat[col] = lat_deg;
                if (bp.lon) row_lon[col] = lon_deg;
                if (bp.local_radius) row_rad[col] = r_km;
                if (bp.resolution) {
                    /* Same approximate formula as flat-field mode (v1 --
                     * see p.phocube.md NOTES). */
                    double circ = 2.0 * M_PI * r_km;
                    row_res[col] = circ / ncols * cos(lat_deg * DEG2RAD);
                }
            }
        }
        else if (camera_mode) {
            /* Camera mode: build a real per-pixel look vector from the
             * instrument's pinhole focal-plane map (same convention as
             * ISIS3's own camera classes: dx = (sample -
             * boresight_sample)*pixel_pitch, dy = (line -
             * boresight_line)*pixel_pitch, dz = focal_length, in the
             * camera frame's own X/Y/Z axes -- CRISM stays 1-D since its
             * boresight_line/k1 are 0 and per-line pointing instead comes
             * from the gimbal CK over time; ISS is a real 2-D framing
             * camera with one static pointing per frame plus genuine
             * radial distortion) and intersect it with the target surface
             * via sincpt -- row/col here are real (line, sample) indices
             * into the raw cube, not coordinates; the surface point (and
             * hence lat/lon) is *found*, not known in advance, unlike -s
             * mode. */
            double et_row = et;
            if (spice_info.have_line_rate)
                et_row = et + (row - (nrows - 1) / 2.0) * spice_info.line_rate;

            double dy = 0.0;
            if (cam.is_framing) {
                double line_1based = row + 1.0;
                dy = (line_1based - cam.boresight_line) * cam.pixel_pitch;
            }

            if (cam.is_omega)
                Rast_get_d_row(mirror_dn_fd, mirror_dn_row, row);

            for (int col = 0; col < ncols; col++) {
                double dvec[3];

                if (cam.is_omega) {
                    /* OMEGA whiskbroom scanning mirror: the boresight
                     * (0,0,1) rotated about the detector frame's +Y axis
                     * by offset_angle = (dn - mirror_center)*mirror_slope
                     * degrees (MEX_OMEGA_V03.TI's "OMEGA Pixels Geometry"
                     * section). Rotation about +Y: x'=sin(theta),
                     * y'=0, z'=cos(theta) for the unit vector (0,0,1). */
                    double dn = mirror_dn_row[col];
                    double offset_deg = (dn - cam.mirror_center) * cam.mirror_slope;
                    double theta = offset_deg * DEG2RAD;
                    double dvec_det[3] = { sin(theta), 0.0, cos(theta) };
                    /* cam.frame is already MEX_SPACECRAFT (see
                     * load_pinhole_camera_model) -- rotate out of the
                     * detector frame into it here. */
                    for (int i = 0; i < 3; i++)
                        dvec[i] = cam.omega_rot[i][0] * dvec_det[0] +
                                  cam.omega_rot[i][1] * dvec_det[1] +
                                  cam.omega_rot[i][2] * dvec_det[2];
                }
                else if (cam.is_vims) {
                    /* Real 2-axis angular scan (ISIS3's
                     * VimsGroundMap::LookDirection(), ported verbatim --
                     * see the comment block above the struct). row/col
                     * here are this cube's own (line, sample), already
                     * offset into the full 64x64 instrument FOV grid by
                     * cam.vims_samp_offset/vims_line_offset. */
                    double x = col + cam.vims_samp_offset;
                    double y = row + cam.vims_line_offset;
                    double theta = (M_PI/2.0) - (y - cam.vims_y_bore) * cam.vims_y_pixsize;
                    double phi = -(M_PI/2.0) + (x - cam.vims_x_bore) * cam.vims_x_pixsize;
                    dvec[0] = sin(theta) * cos(phi);
                    dvec[1] = cos(theta);
                    dvec[2] = -sin(theta) * sin(phi);
                }
                else {
                    double sample_1based = col + 1.0;
                    double dx = (sample_1based - cam.boresight_sample) * cam.pixel_pitch;

                    double r2 = dx * dx + dy * dy;
                    double ux = dx * (1.0 + cam.k1 * r2);
                    double uy = dy * (1.0 + cam.k1 * r2);
                    dvec[0] = ux; dvec[1] = uy; dvec[2] = cam.focal_length;
                }

                double pt[3], srfvec[3], trgepc;
                int hit = p_spice_sincpt(camera_method, target_upper, et_row,
                                         fixref, "LT+S", spice_info.observer,
                                         cam.frame, dvec, pt, &trgepc, srfvec);

                if (hit != 1) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                    if (bp.lat)       Rast_set_d_null_value(&row_lat[col], 1);
                    if (bp.lon)       Rast_set_d_null_value(&row_lon[col], 1);
                    if (bp.local_radius) Rast_set_d_null_value(&row_rad[col], 1);
                    if (bp.resolution)   Rast_set_d_null_value(&row_res[col], 1);
                    continue;
                }

                double r_km = sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
                double lon_deg = atan2(pt[1], pt[0]) * RAD2DEG;
                double lat_deg = asin(pt[2] / r_km) * RAD2DEG;

                double phase_deg, incidence_deg, emission_deg;
                int ok = (p_spice_ilumin(camera_method, target_upper, et_row,
                                         fixref, "LT+S", spice_info.observer,
                                         pt, &phase_deg, &incidence_deg,
                                         &emission_deg) == 0);

                if (!ok) {
                    spice_failed_pixels++;
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                }
                else {
                    if (bp.incidence) row_inc[col] = incidence_deg;
                    if (bp.emission)  row_emi[col] = emission_deg;
                    if (bp.phase)     row_pha[col] = phase_deg;
                }
                if (bp.lat) row_lat[col] = lat_deg;
                if (bp.lon) row_lon[col] = lon_deg;
                if (bp.local_radius) row_rad[col] = r_km;
                if (bp.resolution) {
                    double circ = 2.0 * M_PI * r_km;
                    row_res[col] = circ / ncols * cos(lat_deg * DEG2RAD);
                }
            }
        }
        else {
            /* Flat-field mode: compute look direction from observer to
             * pixel. We derive pixel lat/lon from the GRASS region, then
             * compute the body-fixed XYZ point on the ellipsoid surface
             * at that lat/lon and form the look direction obs→point. */
            for (int col = 0; col < ncols; col++) {
                /* Map pixel centre → easting/northing */
                double east  = region.west  + (col + 0.5) * region.ew_res;
                double north = region.north - (row + 0.5) * region.ns_res;

                /* Convert easting/northing → lat/lon.
                 * If the GRASS location is already geographic (lat/lon), east=lon, north=lat.
                 * For projected locations the user should ensure the region is set correctly;
                 * we treat east/north as lat/lon as a first approximation. */
                double lat_deg = north;
                double lon_deg = east;

                /* Get ellipsoid surface radius at this lat/lon */
                double r_km = p_shape_local_radius_km(shape, lat_deg, lon_deg);

                /* Body-fixed XYZ of surface point */
                double pt[3];
                p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, pt);

                /* Look direction: normalised vector obs → pt */
                double d[3] = { pt[0]-obs[0], pt[1]-obs[1], pt[2]-obs[2] };
                double dlen = sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                if (dlen > 0) { d[0]/=dlen; d[1]/=dlen; d[2]/=dlen; }

                dirs[3*col+0] = d[0];
                dirs[3*col+1] = d[1];
                dirs[3*col+2] = d[2];
            }

            /* Use p_shape_apply_row (OpenMP over columns) */
            p_shape_apply_row(shape, ncols, obs, dirs, sun, results);

            /* Extract results into GRASS row buffers */
            for (int col = 0; col < ncols; col++) {
                PShapeResult *r = &results[col];

                /* Check for valid hit (NaN = ray missed) */
                if (r->incidence != r->incidence) {
                    if (bp.incidence) Rast_set_d_null_value(&row_inc[col], 1);
                    if (bp.emission)  Rast_set_d_null_value(&row_emi[col], 1);
                    if (bp.phase)     Rast_set_d_null_value(&row_pha[col], 1);
                    if (bp.lat)       Rast_set_d_null_value(&row_lat[col], 1);
                    if (bp.lon)       Rast_set_d_null_value(&row_lon[col], 1);
                    if (bp.local_radius) Rast_set_d_null_value(&row_rad[col], 1);
                    if (bp.resolution)   Rast_set_d_null_value(&row_res[col], 1);
                } else {
                    if (bp.incidence)    row_inc[col] = r->incidence;
                    if (bp.emission)     row_emi[col] = r->emission;
                    if (bp.phase)        row_pha[col] = r->phase;
                    if (bp.lat)          row_lat[col] = r->lat;
                    if (bp.lon)          row_lon[col] = r->lon;
                    if (bp.local_radius) row_rad[col] = r->local_radius;
                    if (bp.resolution) {
                        /* Pixel resolution ≈ (ellipsoid circumference / ncols) per pixel
                         * at this latitude — simple approximation. */
                        double circ = 2.0 * M_PI * r->local_radius;
                        row_res[col] = circ / ncols * cos(r->lat * DEG2RAD);
                    }
                }
            }
        }

        if (fd_inc >= 0) Rast_put_d_row(fd_inc, row_inc);
        if (fd_emi >= 0) Rast_put_d_row(fd_emi, row_emi);
        if (fd_pha >= 0) Rast_put_d_row(fd_pha, row_pha);
        if (fd_lat >= 0) Rast_put_d_row(fd_lat, row_lat);
        if (fd_lon >= 0) Rast_put_d_row(fd_lon, row_lon);
        if (fd_rad >= 0) Rast_put_d_row(fd_rad, row_rad);
        if (fd_res >= 0) Rast_put_d_row(fd_res, row_res);
    }
    G_percent(1, 1, 2);

    if ((spice_mode || camera_mode) && spice_failed_pixels > 0)
        G_warning(_("SPICE: %d/%d pixels failed (ray missed the body, or "
                    "ilumin/sincpt error) -- set NULL in output bands."),
                   spice_failed_pixels, nrows * ncols);
    if (spice_mode || camera_mode)
        p_spice_clear();

    if (mirror_dn_fd >= 0) {
        Rast_close(mirror_dn_fd);
        G_free(mirror_dn_row);
    }

    /* ---------------------------------------------------------------- */
    /* Close and write history                                           */
    /* ---------------------------------------------------------------- */
#define CLOSE_BAND(fd, suffix) \
    if (fd >= 0) { \
        Rast_close(fd); \
        snprintf(mapname, sizeof(mapname), "%s_" #suffix, prefix); \
        Rast_short_history(mapname, "raster", &history); \
        Rast_command_history(&history); \
        Rast_write_history(mapname, &history); \
        G_message(_("  Written: %s"), mapname); \
    }

    CLOSE_BAND(fd_inc, incidence);
    CLOSE_BAND(fd_emi, emission);
    CLOSE_BAND(fd_pha, phase);
    CLOSE_BAND(fd_lat, lat);
    CLOSE_BAND(fd_lon, lon);
    CLOSE_BAND(fd_rad, local_radius);
    CLOSE_BAND(fd_res, resolution);

    G_free(dirs); G_free(results);
    G_free(row_inc); G_free(row_emi); G_free(row_pha);
    G_free(row_lat); G_free(row_lon); G_free(row_rad); G_free(row_res);
    p_shape_free(shape);

    G_message(_("p.phocube complete."));
    return EXIT_SUCCESS;
}
