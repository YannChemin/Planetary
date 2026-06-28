/****************************************************************************
 *
 * MODULE:       p.cam2map
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Project a raw (camera-geometry) planetary image onto a
 *               map-projected (real lat/lon) GRASS raster, using a real
 *               NAIF SPICE camera model to back-project each output pixel
 *               into the raw input image.
 *
 *               -c (camera mode): for each output pixel (real lat/lon from
 *               the current geographic region), finds the body-fixed
 *               surface point (NAIF latsrf), projects it back through the
 *               instrument's pinhole camera model (boresight/pixel-pitch/
 *               focal-length/radial distortion, read from the loaded IK/IAK
 *               -- same model and same real, ISIS3-sourced IAK values as
 *               p.phocube's -c) to a fractional (sample, line) in the raw
 *               input image, and bilinearly samples the input DN there.
 *               ISS_NAC/ISS_WAC: closed-form framing inverse (single epoch).
 *               CRISM_VNIR/CRISM_IR: per-line epoch binary search (pushbroom).
 *               Requires p.spiceinit to have attached target/observer/time/
 *               kernels to the input map.
 *               The output region's north/south/east/west are interpreted
 *               directly as real lat/lon degrees, by convention -- run
 *               this in the same (typically PROJECTION_XY) location as
 *               the input, same as p.phocube's own camera-mode tests;
 *               see NOTES.
 *
 *               Without -c: a naive direct lat/lon<->pixel resample using
 *               the input raster's own region as if it were already real
 *               lat/lon (no SPICE, no camera model at all -- only correct
 *               if the input has genuinely already been map-projected by
 *               some other means). Kept for back-compatibility/testing;
 *               see NOTES.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "../../libs/p_shapemodel/p_shapemodel.h"
#include "../../libs/p_spice/p_spice.h"
#include "../../libs/p_meta/p_meta.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI/180.0)
#define RAD2DEG (180.0/M_PI)

/* ------------------------------------------------------------------ */
/* Camera mode (-c): real SPICE back-projection, ISS_NAC/ISS_WAC only  */
/*                                                                      */
/* Cassini ISS NAC/WAC is the only camera shape implemented so far:    */
/* a static, single-epoch pinhole framing camera (one boresight        */
/* pointing for the whole frame, real radial distortion K1, focal      */
/* length resolved per real filter pair) -- the same model and the     */
/* same real ISIS3-sourced instrument addendum kernel (IAK) values     */
/* p.phocube's -c instrument=ISS_NAC/ISS_WAC already uses and has       */
/* verified against real Cassini ISS data (see p.phocube.md).          */
/*                                                                      */
/* Back-projection is the exact algebraic inverse of p.phocube's        */
/* forward ray construction:                                            */
/*   forward: dx=(sample-boresight_sample)*pixel_pitch,                 */
/*            dy=(line-boresight_line)*pixel_pitch,                     */
/*            r2=dx*dx+dy*dy, ux=dx*(1+K1*r2), uy=dy*(1+K1*r2),         */
/*            ray=(ux,uy,focal_length) in the camera frame, then        */
/*            sincpt(ray) -> body-fixed surface point.                  */
/*   inverse (this module): take the known body-fixed surface point     */
/*            (from latsrf, given the output pixel's real lat/lon),     */
/*            recover the camera-frame ray by vector subtraction +      */
/*            pxform, scale its Z component to focal_length to get      */
/*            (ux,uy), invert the (tiny, K1 ~ 1e-5..1e-4) radial         */
/*            distortion by a few fixed-point iterations               */
/*            (dx=ux/(1+K1*(dx*dx+dy*dy)), converges in ~3 steps for    */
/*            ISS's real K1/FOV), then sample=boresight_sample+dx/      */
/*            pixel_pitch, line=boresight_line+dy/pixel_pitch.          */
/*                                                                      */
/* CRISM (per-line gimbal CK), MEX OMEGA (whiskbroom scanning mirror)   */
/* and Cassini VIMS (2-axis angular scan) all have time- and/or         */
/* sample-varying pointing -- inverting them needs a 1-D or 2-D         */
/* root-search over acquisition time/mirror position, not just a       */
/* closed-form algebraic inverse. Deliberately not implemented yet      */
/* (see TODO.md) -- instrument= fails loudly for them, not a guess.     */
/* ------------------------------------------------------------------ */
/* IssCameraModel covers ISS/CRISM/OMEGA/VIMS/CTX/LROC_NAC/HRSC.
 * ISS:      is_pushbroom=0, real K1 distortion, per-filter focal length.
 * CRISM:    is_pushbroom=1, along-track=Y, cross-track=X (pushbroom_x_sign=+1).
 * OMEGA:    is_omega=1, is_pushbroom=1, per-line epoch + mirror scan.
 * VIMS:     is_vims=1, closed-form 2-axis angular scan inverse (no pushbroom).
 * CTX:      is_ctx=1, is_pushbroom=1, along-track=X, OD_K[3] distortion.
 * LROC_NAC: is_lroc_nac=1, is_pushbroom=1, along-track=X, custom K1 model;
 *            pushbroom_y_sign=+1 (NACL) or -1 (NACR, flipped cross-track).
 * HRSC_ND3: is_pushbroom=1, along-track=Y (same as CRISM), no distortion,
 *            frame=MEX_HRSC_HEAD (-41210), pushbroom_x_sign=-1, focal_plane_x0≠0. */
typedef struct {
    int    naif_id;
    char   frame[64];
    double boresight_sample;
    double boresight_line;
    double pixel_pitch;
    double focal_length;
    double k1;
    int    is_pushbroom; /* 1=CRISM/OMEGA/CTX per-line epoch, 0=ISS/VIMS */
    int    is_omega;     /* 1=OMEGA whiskbroom mirror scan */
    double omega_rot[3][3]; /* detector frame → MEX_SPACECRAFT rotation */
    double mirror_center;   /* INS-41420_MIRROR_CENTER_POSITION */
    double mirror_slope;    /* INS-41420_MIRROR_SLOPE (deg/DN) */
    int    is_vims;      /* 1=Cassini VIMS 2-axis angular scan */
    double vims_x_pixsize;  /* rad/pixel, cross-track (sample) axis */
    double vims_y_pixsize;  /* rad/pixel, down-track (line) axis */
    double vims_x_bore;     /* boresight sample in 64x64 FOV grid */
    double vims_y_bore;     /* boresight line in 64x64 FOV grid */
    int    vims_samp_offset; /* swath sample offset into 64x64 grid */
    int    vims_line_offset; /* swath line offset into 64x64 grid */
    int    is_ctx;       /* 1=MRO CTX: along-track=X, cross-track=Y, OD_K */
    double odk[3];       /* CTX radial distortion: drOverR=odk[0]+r²*(odk[1]+r²*odk[2]) */
    int    is_lroc_nac;  /* 1=LRO LROC NAC-L/NAC-R: along-track=X, custom K1 distortion */
    double nac_k1;       /* LROC NAC: single OD_K coeff; model: uy=dy/(1+K1*dy²) */
    int    pushbroom_y_sign; /* +1=NACL/CTX (Y cross-track positive), -1=NACR (Y flipped) */
    /* HRSC/CRISM generic cross-track:
     *   sample = boresight_sample + pushbroom_x_sign*(x_fp - focal_plane_x0)/pixel_pitch - 1
     * CRISM: pushbroom_x_sign=+1, focal_plane_x0=0 (TRANSX[1]=+pixel_pitch, offset=0).
     * HRSC:  pushbroom_x_sign=-1, focal_plane_x0=TRANSX[0] (TRANSX[1]=-pixel_pitch). */
    int    pushbroom_x_sign;  /* +1 CRISM, -1 HRSC (sign of TRANSX[1] / pixel_pitch) */
    double focal_plane_x0;   /* TRANSX[0] constant focal-plane X offset (mm); 0 for CRISM */
} IssCameraModel;

static void load_iss_camera_model(const char *instrument,
                                   const char *input_map,
                                   const char *filter1, const char *filter2,
                                   const char *sampling_mode,
                                   const char *x_offset_s,
                                   const char *z_offset_s,
                                   const char *swath_width_s,
                                   const char *swath_length_s,
                                   int image_cols, int image_rows,
                                   IssCameraModel *cam)
{
    memset(cam, 0, sizeof(*cam));

    if (strcmp(instrument, "OMEGA_SWIR_C") == 0) {
        cam->naif_id = -41421;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_SWIR_C");
        cam->is_pushbroom = 1;
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "OMEGA_SWIR_L") == 0) {
        cam->naif_id = -41422;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_SWIR_L");
        cam->is_pushbroom = 1;
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "OMEGA_VNIR") == 0) {
        cam->naif_id = -41410;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_OMEGA_VNIR");
        cam->is_pushbroom = 1;
        cam->is_omega = 1;
    }
    else if (strcmp(instrument, "CRISM_VNIR") == 0) {
        cam->naif_id = -74017;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_VNIR");
        cam->is_pushbroom = 1;
        cam->pushbroom_x_sign = 1;  /* TRANSX[1]=+pixel_pitch */
    }
    else if (strcmp(instrument, "CRISM_IR") == 0) {
        cam->naif_id = -74018;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CRISM_IR");
        cam->is_pushbroom = 1;
        cam->pushbroom_x_sign = 1;
    }
    else if (strcmp(instrument, "HRSC_ND3") == 0) {
        /* Nadir channel (-41215) but rotate via the shared MEX_HRSC_HEAD frame (-41210).
         * TRANSX[1]=-pixel_pitch (reversed cross-track vs CRISM); TRANSX[0]≠0. */
        cam->naif_id = -41215;
        snprintf(cam->frame, sizeof(cam->frame), "MEX_HRSC_HEAD");
        cam->is_pushbroom = 1;
        cam->pushbroom_x_sign = -1;
    }
    else if (strcmp(instrument, "CTX") == 0) {
        cam->naif_id = -74021;
        snprintf(cam->frame, sizeof(cam->frame), "MRO_CTX");
        cam->is_pushbroom = 1;
        cam->is_ctx = 1;
        cam->pushbroom_y_sign = 1;
    }
    else if (strcmp(instrument, "LROC_NACL") == 0) {
        cam->naif_id = -85600;
        snprintf(cam->frame, sizeof(cam->frame), "LRO_LROCNACL");
        cam->is_pushbroom = 1;
        cam->is_lroc_nac = 1;
        cam->pushbroom_y_sign = 1;
    }
    else if (strcmp(instrument, "LROC_NACR") == 0) {
        cam->naif_id = -85610;
        snprintf(cam->frame, sizeof(cam->frame), "LRO_LROCNACR");
        cam->is_pushbroom = 1;
        cam->is_lroc_nac = 1;
        cam->pushbroom_y_sign = -1;
    }
    else if (strcmp(instrument, "ISS_NAC") == 0) {
        cam->naif_id = -82360;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_NAC_USGS");
    }
    else if (strcmp(instrument, "ISS_WAC") == 0) {
        cam->naif_id = -82361;
        snprintf(cam->frame, sizeof(cam->frame), "CASSINI_ISS_WAC_USGS");
    }
    else if (strcmp(instrument, "VIMS_IR")  == 0 ||
             strcmp(instrument, "VIMS_VIS") == 0) {
        int is_ir = (strcmp(instrument, "VIMS_IR") == 0);
        cam->naif_id = is_ir ? -82371 : -82370;  /* corrected IAK IDs */
        snprintf(cam->frame, sizeof(cam->frame),
                 is_ir ? "CASSINI_VIMS_IR" : "CASSINI_VIMS_V");
        cam->is_vims = 1;

        /* Resolve sampling mode: CLI override > planetary.json */
        char samp_mode[16] = "";
        if (sampling_mode && sampling_mode[0])
            snprintf(samp_mode, sizeof(samp_mode), "%s", sampling_mode);
        else {
            const char *field = is_ir ? "sampling_mode_ir" : "sampling_mode_vis";
            char buf[32];
            if (p_meta_read_string_field(input_map, "raster", field,
                                         buf, sizeof(buf)) == 0)
                snprintf(samp_mode, sizeof(samp_mode), "%s", buf);
        }
        if (!samp_mode[0])
            G_fatal_error(_("VIMS_%s requires sampling_mode= (NORMAL or HI-RES) "
                            "-- pass it explicitly or import via p.in.archive "
                            "vims= (which writes it to planetary.json)."),
                           is_ir ? "IR" : "VIS");

        /* Resolve swath metadata: CLI > planetary.json */
        const char *vims_fields[4]    = {"x_offset","z_offset","swath_width","swath_length"};
        const char *vims_overrides[4] = {x_offset_s, z_offset_s, swath_width_s, swath_length_s};
        long x_offset = 1, z_offset = 1, swath_width = 64, swath_length = 64;
        long *vims_out[4] = {&x_offset, &z_offset, &swath_width, &swath_length};
        for (int i = 0; i < 4; i++) {
            if (vims_overrides[i] && vims_overrides[i][0]) {
                *vims_out[i] = atol(vims_overrides[i]);
            } else {
                char buf[32];
                if (p_meta_read_string_field(input_map, "raster", vims_fields[i],
                                              buf, sizeof(buf)) == 0)
                    *vims_out[i] = atol(buf);
                else
                    G_fatal_error(_("VIMS_%s requires %s= -- pass it explicitly "
                                    "or import via p.in.archive vims=."),
                                   is_ir ? "IR" : "VIS", vims_fields[i]);
            }
        }

        int hires = (strcasecmp(samp_mode, "HI-RES") == 0);
        int camSampOffset, camLineOffset;
        double xPixSize, yPixSize, xBore, yBore;

        if (!is_ir) {            /* VIMS_VIS (channel V) */
            if (!hires) {
                xPixSize = yPixSize = 0.00051;
                xBore = yBore = 31;
                camSampOffset = (int)x_offset - 1;
                camLineOffset = (int)z_offset - 1;
            } else {
                xPixSize = yPixSize = 0.00051 / 3.0;
                xBore = yBore = 94;
                camSampOffset = (3 * ((int)x_offset + (int)swath_width / 2)) -
                                (int)swath_width / 2;
                camLineOffset = (3 * ((int)z_offset + (int)swath_length / 2)) -
                                (int)swath_length / 2;
            }
        } else {                /* VIMS_IR */
            if (!hires) {
                xPixSize = yPixSize = 0.000495;
                xBore = yBore = 31;
                camSampOffset = (int)x_offset - 1;
                camLineOffset = (int)z_offset - 1;
            } else {
                xPixSize = 0.000495 / 2.0;
                yPixSize = 0.000495;
                xBore = 62.5;
                yBore = 31;
                camSampOffset = 2 * (((int)x_offset - 1) +
                                     (((int)swath_width - 1) / 4));
                camLineOffset = (int)z_offset - 1;
            }
        }

        cam->vims_x_pixsize  = xPixSize;
        cam->vims_y_pixsize  = yPixSize;
        cam->vims_x_bore     = xBore;
        cam->vims_y_bore     = yBore;
        cam->vims_samp_offset = camSampOffset;
        cam->vims_line_offset = camLineOffset;
        return;
    }
    else
        G_fatal_error(_("Camera mode (-c): unsupported instrument='%s' "
                        "(supported: CRISM_VNIR, CRISM_IR, OMEGA_SWIR_C, "
                        "OMEGA_SWIR_L, OMEGA_VNIR, CTX, LROC_NACL, LROC_NACR, "
                        "HRSC_ND3, VIMS_IR, VIMS_VIS, ISS_NAC, ISS_WAC)."),
                       instrument);

    char varname[80];
    int n;

    const char *iak_hint = cam->is_pushbroom
        ? (cam->is_ctx        ? "the MROCTX addendum kernel (mroctxAddendum005.ti)"
         : cam->is_lroc_nac   ? "the LRO LROC IK (lro_lroc_v20.ti)"
         : cam->naif_id==-41215 ? "the HRSC IAK (hrscAddendum004.ti)"
                                : "the CRISM addendum kernel (crismAddendum001.ti)")
        : "the IssNAAddendum/IssWAAddendum instrument addendum kernel";

    snprintf(varname, sizeof(varname), "INS%d_PIXEL_PITCH", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->pixel_pitch) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                        "IK -- has %s been attached via p.spiceinit's ik=, "
                        "in addition to the regular IK?"), varname, iak_hint);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_SAMPLE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_sample) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    snprintf(varname, sizeof(varname), "INS%d_BORESIGHT_LINE", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->boresight_line) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    /* Pushbroom instruments: no K1, no ISS summing correction, no per-filter
     * focal length.  OMEGA also needs mirror params; CTX also needs OD_K. */
    if (cam->is_pushbroom) {
        if (cam->is_omega) {
            /* Mirror parameters live under the shared SWIR id (-41420) --
             * one physical mirror drives all three OMEGA channels. */
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
            /* Pre-rotate from the channel's detector frame into MEX_SPACECRAFT
             * (fixed TKFRAME chain, et=0 is fine). cam.frame becomes
             * MEX_SPACECRAFT for all subsequent pxform calls. */
            if (p_spice_pxform(cam->frame, "MEX_SPACECRAFT", 0.0,
                                cam->omega_rot) < 0)
                G_fatal_error(_("Camera mode (-c): pxform('%s' -> "
                                "'MEX_SPACECRAFT') failed -- is the MEX "
                                "frame kernel (MEX_V16.TF) attached via "
                                "p.spiceinit's fk=?"), cam->frame);
            snprintf(cam->frame, sizeof(cam->frame), "MEX_SPACECRAFT");
            return;
        }
        if (cam->is_ctx) {
            /* CTX: focal length + 3-coefficient OD_K radial distortion.
             * Along-track = X axis (dvec[0]); cross-track = Y (dvec[1]). */
            snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
            if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
                G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                                "IK -- has mroctxAddendum005.ti been attached via "
                                "p.spiceinit's ik=?"), varname);
            snprintf(varname, sizeof(varname), "INS%d_OD_K", cam->naif_id);
            if (p_spice_gdpool_d(varname, 0, 3, &n, cam->odk) < 0 || n != 3)
                G_fatal_error(_("Camera mode (-c): could not read %s (3 values) "
                                "from the loaded IK."), varname);
            return;
        }
        if (cam->naif_id == -41215) {
            /* HRSC ND3: FOCAL_LENGTH from channel IAK (-41215); PIXEL_PITCH is
             * shared under -41210 (already loaded above from INS-41215_PIXEL_PITCH
             * — note: the shared INS-41210_PIXEL_PITCH is the canonical value, but
             * the channel IAK also defines it; either works here). Load TRANSX
             * to get the constant focal-plane X offset (TRANSX[0]) for the
             * boresight correction. No OD_K distortion (all zeros). */
            snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
            if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
                G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                                "IK -- has hrscAddendum004.ti been attached via "
                                "p.spiceinit's ik=?"), varname);
            /* TRANSX[0] is the constant focal-plane X offset at boresight_sample. */
            snprintf(varname, sizeof(varname), "INS%d_TRANSX", cam->naif_id);
            double transx_buf[3] = {0.0, 0.0, 0.0};
            if (p_spice_gdpool_d(varname, 0, 3, &n, transx_buf) == 0 && n >= 1)
                cam->focal_plane_x0 = transx_buf[0];
            /* pushbroom_x_sign=-1 already set in dispatch; no distortion. */
            return;
        }
        if (cam->is_lroc_nac) {
            /* LROC NAC-L/NAC-R: focal length + single OD_K coefficient.
             * Distortion model (LroNarrowAngleDistortionMap): uy = dy/(1+K1*dy²).
             * Along-track = X (dvec[0]); cross-track = Y (dvec[1]).
             * NACR has pushbroom_y_sign=-1 (ITRANSS[2]=-142.857). */
            snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
            if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
                G_fatal_error(_("Camera mode (-c): could not read %s from the loaded "
                                "IK -- has lro_lroc_v20.ti been attached via "
                                "p.spiceinit's ik=?"), varname);
            snprintf(varname, sizeof(varname), "INS%d_OD_K", cam->naif_id);
            if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->nac_k1) < 0 || n != 1)
                G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                               varname);
            return;
        }
        /* CRISM: single FOCAL_LENGTH scalar */
        snprintf(varname, sizeof(varname), "INS%d_FOCAL_LENGTH", cam->naif_id);
        if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->focal_length) < 0 || n != 1)
            G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                           varname);
        return;
    }

    snprintf(varname, sizeof(varname), "INS%d_K1", cam->naif_id);
    if (p_spice_gdpool_d(varname, 0, 1, &n, &cam->k1) < 0 || n != 1)
        G_fatal_error(_("Camera mode (-c): could not read %s from the loaded IK."),
                       varname);

    /* BORESIGHT_SAMPLE/LINE and PIXEL_PITCH in the IK/IAK are given in the
     * full-resolution (1x1) detector frame (e.g. 1024x1024 for ISS NAC/
     * WAC), but real images are often acquired in a SUMMED (binned) mode
     * (e.g. INSTRUMENT_MODE_ID="SUM2" -> 512x512) -- using the raw IK
     * values directly against a summed image's own pixel coordinates
     * silently misplaces every ray by the summing factor (confirmed via
     * a real SUM2 Cassini ISS NAC frame: produced a 0% back-projection
     * hit rate even at the frame's own forward-computed centre lat/lon).
     * Detect the summing factor by comparing the IK's own full-frame
     * PIXEL_SAMPLES/PIXEL_LINES to this image's actual dimensions --
     * more robust than parsing INSTRUMENT_MODE_ID text, and needs no
     * extra metadata plumbing. */
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

    /* Focal length varies per filter pair -- resolve filter1/filter2 from
     * -c's own options, else the raster's p_meta sidecar, else fall back
     * to the IAK's own DEFAULT_FOCAL_LENGTH (documented fallback, same as
     * p.phocube's ISS path). */
    char f1[32] = "", f2[32] = "";
    if (filter1) snprintf(f1, sizeof(f1), "%s", filter1);
    if (filter2) snprintf(f2, sizeof(f2), "%s", filter2);

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
                        "using %s."), varname);
    }
}

/* SPICE history metadata, written by p.spiceinit -- target/observer/time
 * are required; kernels are loaded as found. Mirrors p.phocube's own
 * read_spice_history() (not shared via a library -- see that module for
 * the line_rate/DSK variants this camera shape doesn't need). */
typedef struct {
    char   target[64];
    char   observer[64];
    char   time[64];
    double line_rate;
    int    have_target, have_observer, have_time, have_line_rate;
    int    n_kernels_loaded;
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
        if (strncmp(line, "SPICE_", 6) != 0)
            continue;
        const char *kv = line + 6;
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
        else if (strcmp(key, "LSK") == 0 || strcmp(key, "SCLK") == 0 ||
                 strcmp(key, "CK") == 0 || strcmp(key, "SPK") == 0 ||
                 strcmp(key, "IK") == 0 || strcmp(key, "FK") == 0 ||
                 strcmp(key, "PCK") == 0 || strcmp(key, "DSK") == 0) {
            spice_load_paths(value, &info.n_kernels_loaded);
        }
    }
    Rast_free_history(&hist);

    if (!info.have_target || !info.have_observer || !info.have_time)
        G_fatal_error(_("Camera mode (-c) requires target=, observer= and "
                        "time= to have been attached to <%s> via "
                        "p.spiceinit (found: target=%s observer=%s time=%s)."),
                       mapname,
                       info.have_target ? info.target : "(missing)",
                       info.have_observer ? info.observer : "(missing)",
                       info.have_time ? info.time : "(missing)");
    if (info.n_kernels_loaded == 0)
        G_fatal_error(_("Camera mode (-c) found no loadable kernels in <%s>'s "
                        "history -- run p.spiceinit with lsk=/spk=/etc first."),
                       mapname);

    return info;
}

/* Compute the camera-frame direction vector to a body-fixed surface point
 * at a given epoch (one scan line's worth of SPICE work: spkpos + two
 * pxform + two matrix-vector multiplies).  Returns 0 on success, -1 on
 * any SPICE failure.  Used by the CRISM pushbroom binary search. */
static int eval_pushbroom_frame(const double spoint[3],
                                 double et_line,
                                 const char *fixref,
                                 const char *cam_frame,
                                 const char *target,
                                 const char *observer,
                                 double dvec_out[3])
{
    double pos[3], lt;
    if (p_spice_pos(target, et_line, fixref, "LT+S", observer, pos, &lt) < 0)
        return -1;

    double ray[3] = { spoint[0] + pos[0],
                      spoint[1] + pos[1],
                      spoint[2] + pos[2] };

    double rot_fix2j2000[3][3], rot_j2000_2cam[3][3];
    if (p_spice_pxform(fixref, "J2000", et_line - lt, rot_fix2j2000) < 0)
        return -1;
    if (p_spice_pxform("J2000", cam_frame, et_line, rot_j2000_2cam) < 0)
        return -1;

    double inertial[3];
    for (int i = 0; i < 3; i++)
        inertial[i] = rot_fix2j2000[i][0]*ray[0]
                    + rot_fix2j2000[i][1]*ray[1]
                    + rot_fix2j2000[i][2]*ray[2];
    for (int i = 0; i < 3; i++)
        dvec_out[i] = rot_j2000_2cam[i][0]*inertial[0]
                    + rot_j2000_2cam[i][1]*inertial[1]
                    + rot_j2000_2cam[i][2]*inertial[2];
    return 0;
}

static void uppercase_copy(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Option  *opt_a, *opt_b, *opt_c;
    struct Option  *opt_interp;
    struct Option  *opt_instrument, *opt_filter1, *opt_filter2, *opt_mirror_dn;
    struct Option  *opt_sampling_mode, *opt_x_offset, *opt_z_offset;
    struct Option  *opt_swath_width, *opt_swath_length;
    struct Option  *opt_projection, *opt_clon;
    struct Flag    *flag_camera;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Camera & Geometry"));
    G_add_keyword(_("projection"));
    G_add_keyword(_("camera"));
    G_add_keyword(_("map"));
    module->label       = _("Project a raw planetary camera image to a real map grid.");
    module->description = _("With -c: for each output pixel (real lat/lon from the "
                             "current geographic region), back-projects through a real "
                             "NAIF SPICE camera model (boresight/pixel-pitch/focal-length/ "
                             "radial distortion, read from the IK/IAK p.spiceinit attached) "
                             "to a fractional (sample, line) in the raw input image, and "
                             "bilinearly samples the input DN there. Requires instrument= "
                             "and p.spiceinit to have been run. Without -c: a naive direct "
                             "lat/lon<->pixel resample using the input's own region, with no "
                             "SPICE involved at all -- only meaningful if the input has "
                             "already been map-projected by some other means. See NOTES.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_input->description = _("Input raw (camera-geometry) raster");
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Output map-projected raster");

    flag_camera = G_define_flag();
    flag_camera->key = 'c';
    flag_camera->label = _("Camera mode: real SPICE back-projection");
    flag_camera->description = _("Requires instrument= and a p.spiceinit'd input; "
                                  "the output region's bounds are interpreted "
                                  "directly as real lat/lon degrees. See NOTES.");

    opt_instrument = G_define_option();
    opt_instrument->key      = "instrument";
    opt_instrument->type     = TYPE_STRING;
    opt_instrument->required = NO;
    opt_instrument->options  = "CRISM_VNIR,CRISM_IR,OMEGA_SWIR_C,OMEGA_SWIR_L,OMEGA_VNIR,CTX,LROC_NACL,LROC_NACR,HRSC_ND3,VIMS_IR,VIMS_VIS,ISS_NAC,ISS_WAC";
    opt_instrument->description = _("Instrument camera model to use with -c. "
        "CRISM_VNIR/CRISM_IR: MRO CRISM pushbroom (per-line epoch, requires "
        "line_rate= in p.spiceinit history). "
        "CTX: MRO Context Camera pushbroom (per-line epoch, OD_K distortion, "
        "requires line_rate= and mroctxAddendum005.ti via ik=). "
        "LROC_NACL/LROC_NACR: LRO Narrow Angle Camera pushbroom (per-line epoch, "
        "single K1 LroNarrowAngleDistortionMap, requires line_rate= and "
        "lro_lroc_v20.ti via ik=; NACR has flipped cross-track axis). "
        "HRSC_ND3: MEX HRSC nadir pushbroom (along-track=Y, reversed cross-track, "
        "TRANSX[0] offset; requires line_rate= and hrscAddendum004.ti via ik=). "
        "ISS_NAC/ISS_WAC: Cassini ISS framing camera (closed-form inverse).");

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
    opt_mirror_dn->description = _("OMEGA_SWIR_C/OMEGA_SWIR_L/OMEGA_VNIR: "
        "mirror-DN sideplane raster (same dimensions as input, one mirror "
        "position per pixel -- written by p.in.pds3 -g for OMEGA cubes)");

    opt_sampling_mode = G_define_option();
    opt_sampling_mode->key      = "sampling_mode";
    opt_sampling_mode->type     = TYPE_STRING;
    opt_sampling_mode->required = NO;
    opt_sampling_mode->options  = "NORMAL,HI-RES";
    opt_sampling_mode->description = _("VIMS_IR/VIMS_VIS: sampling mode "
        "(if not stored in planetary.json by p.in.archive vims=)");

    opt_x_offset = G_define_option();
    opt_x_offset->key      = "x_offset";
    opt_x_offset->type     = TYPE_INTEGER;
    opt_x_offset->required = NO;
    opt_x_offset->description = _("VIMS_IR/VIMS_VIS: X_OFFSET from the cube label "
        "(swath start sample in the 64x64 FOV grid)");

    opt_z_offset = G_define_option();
    opt_z_offset->key      = "z_offset";
    opt_z_offset->type     = TYPE_INTEGER;
    opt_z_offset->required = NO;
    opt_z_offset->description = _("VIMS_IR/VIMS_VIS: Z_OFFSET from the cube label "
        "(swath start line in the 64x64 FOV grid)");

    opt_swath_width = G_define_option();
    opt_swath_width->key      = "swath_width";
    opt_swath_width->type     = TYPE_INTEGER;
    opt_swath_width->required = NO;
    opt_swath_width->description = _("VIMS_IR/VIMS_VIS: SWATH_WIDTH from the cube label");

    opt_swath_length = G_define_option();
    opt_swath_length->key      = "swath_length";
    opt_swath_length->type     = TYPE_INTEGER;
    opt_swath_length->required = NO;
    opt_swath_length->description = _("VIMS_IR/VIMS_VIS: SWATH_LENGTH from the cube label");

    opt_a = G_define_option(); opt_a->key="a_radius"; opt_a->type=TYPE_DOUBLE;
    opt_a->required=NO; opt_a->answer="3396.19";
    opt_a->description=_("Without -c: ellipsoid semi-major radius [km]");
    opt_b = G_define_option(); opt_b->key="b_radius"; opt_b->type=TYPE_DOUBLE;
    opt_b->required=NO; opt_b->answer="3396.19";
    opt_b->description=_("Without -c: ellipsoid b radius [km]");
    opt_c = G_define_option(); opt_c->key="c_radius"; opt_c->type=TYPE_DOUBLE;
    opt_c->required=NO; opt_c->answer="3376.20";
    opt_c->description=_("Without -c: ellipsoid polar radius [km]");

    opt_interp = G_define_option(); opt_interp->key="method"; opt_interp->type=TYPE_STRING;
    opt_interp->required=NO; opt_interp->answer="bilinear";
    opt_interp->options="nearest,bilinear";
    opt_interp->description=_("Resampling method");

    opt_projection = G_define_option();
    opt_projection->key         = "projection";
    opt_projection->type        = TYPE_STRING;
    opt_projection->required    = NO;
    opt_projection->answer      = "latlon";
    opt_projection->options     = "latlon,sinusoidal,stereo_north,stereo_south";
    opt_projection->description = _("Output map projection (camera mode only). "
        "latlon: output north/south/east/west are plain lat/lon degrees (default). "
        "sinusoidal: equal-area; output north/south are lat degrees, "
        "east/west are (lon-clon)*cos(lat) degrees. "
        "stereo_north/stereo_south: polar stereographic; output east/west "
        "are sin(lon-clon)*tan(pi/4-lat/2)*180/pi degrees from the pole, "
        "north/south are -cos(lon-clon)*tan(pi/4-lat/2)*180/pi degrees. "
        "For all non-latlon projections, set clon= to the central meridian.");

    opt_clon = G_define_option();
    opt_clon->key         = "clon";
    opt_clon->type        = TYPE_DOUBLE;
    opt_clon->required    = NO;
    opt_clon->answer      = "0";
    opt_clon->description = _("Central longitude (degrees East) for sinusoidal "
                               "or polar stereographic projections");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    int camera_mode = flag_camera->answer;

    if (camera_mode && !opt_instrument->answer)
        G_fatal_error(_("-c requires instrument= (CRISM_VNIR, CRISM_IR, "
                        "OMEGA_SWIR_C, OMEGA_SWIR_L, OMEGA_VNIR, "
                        "VIMS_IR, VIMS_VIS, ISS_NAC, or ISS_WAC)."));

    double a_km = atof(opt_a->answer);
    double b_km = atof(opt_b->answer);
    double c_km = atof(opt_c->answer);
    int bilinear = (strcmp(opt_interp->answer, "bilinear") == 0);
    double clon_deg = atof(opt_clon->answer);
    const char *proj_name = opt_projection->answer ? opt_projection->answer : "latlon";
    int proj_sinusoidal  = (strcmp(proj_name, "sinusoidal")  == 0);
    int proj_stereo_n    = (strcmp(proj_name, "stereo_north") == 0);
    int proj_stereo_s    = (strcmp(proj_name, "stereo_south") == 0);

    /* Open input raster, find its mapset (needed for camera mode's
     * history read), and load it fully into memory for random-access
     * resampling -- both modes need this. */
    const char *input_mapset = G_find_raster(opt_input->answer, "");
    if (!input_mapset)
        G_fatal_error(_("Raster map <%s> not found"), opt_input->answer);

    struct Cell_head in_region, out_region;
    Rast_get_cellhd(opt_input->answer, input_mapset, &in_region);
    G_get_window(&out_region);

    int in_rows  = in_region.rows;
    int in_cols  = in_region.cols;
    int out_rows = out_region.rows;
    int out_cols = out_region.cols;

    G_message(_("Input: %d x %d -> Output: %d x %d"),
               in_rows, in_cols, out_rows, out_cols);

    /* Rast_get_d_row()/Rast_put_d_row() resample against the raster
     * library's OWN window cache (R__.rd_window/wr_window), which is
     * distinct from the GIS library's G__.window and is only synced by
     * Rast_set_window() (G_set_window() alone leaves it stale at
     * whatever it was lazily initialised to on the first raster open).
     * So: Rast_set_window(&in_region) for an exact, unresampled native-
     * pixel read of the input, then Rast_set_window(&out_region) before
     * Rast_open_new()/writing below. */
    Rast_set_window(&in_region);
    int fd_in = Rast_open_old(opt_input->answer, input_mapset);
    DCELL **in_data = (DCELL **)G_malloc((size_t)in_rows * sizeof(DCELL *));
    DCELL *in_buf = Rast_allocate_d_buf();
    for (int r = 0; r < in_rows; r++) {
        in_data[r] = (DCELL *)G_malloc((size_t)in_cols * sizeof(DCELL));
        Rast_get_d_row(fd_in, in_buf, r);
        memcpy(in_data[r], in_buf, (size_t)in_cols * sizeof(DCELL));
    }
    Rast_close(fd_in);
    G_free(in_buf);

    /* Load mirror_dn sideplane raster if supplied (OMEGA camera mode).
     * Must have the same dimensions as the input raster. */
    DCELL **mirror_dn_data = NULL;
    if (opt_mirror_dn->answer) {
        const char *mdn_mapset = G_find_raster(opt_mirror_dn->answer, "");
        if (!mdn_mapset)
            G_fatal_error(_("mirror_dn raster <%s> not found"),
                           opt_mirror_dn->answer);
        struct Cell_head mdn_region;
        Rast_get_cellhd(opt_mirror_dn->answer, mdn_mapset, &mdn_region);
        if (mdn_region.rows != in_rows || mdn_region.cols != in_cols)
            G_fatal_error(_("mirror_dn raster <%s> is %dx%d but input is "
                            "%dx%d -- they must have identical dimensions."),
                           opt_mirror_dn->answer, mdn_region.rows,
                           mdn_region.cols, in_rows, in_cols);
        Rast_set_window(&in_region);
        int fd_mdn = Rast_open_old(opt_mirror_dn->answer, mdn_mapset);
        DCELL *mdn_buf = Rast_allocate_d_buf();
        mirror_dn_data = (DCELL **)G_malloc((size_t)in_rows * sizeof(DCELL *));
        for (int r = 0; r < in_rows; r++) {
            mirror_dn_data[r] = (DCELL *)G_malloc((size_t)in_cols * sizeof(DCELL));
            Rast_get_d_row(fd_mdn, mdn_buf, r);
            memcpy(mirror_dn_data[r], mdn_buf, (size_t)in_cols * sizeof(DCELL));
        }
        Rast_close(fd_mdn);
        G_free(mdn_buf);
    }

    Rast_set_window(&out_region);

    /* Camera mode (-c) setup: real target/observer/time/kernels from the
     * input's own p.spiceinit history, plus the ISS pinhole camera model.
     * Output must be a real geographic grid -- the whole point of this
     * mode is genuine map reprojection, unlike p.phocube's -c (which
     * keeps the raw pixel/line grid and only attaches geometry as extra
     * backplane VALUES). */
    IssCameraModel cam;
    double et = 0.0;
    char target_upper[64] = "", fixref[80] = "";
    SpiceHistoryInfo spice_info;

    if (camera_mode) {
        /* The output grid's north/south/east/west are interpreted directly
         * as real planetocentric lat/lon degrees, by convention -- not via
         * GRASS's own CRS machinery. This is deliberate, not an oversight:
         * a real PROJECTION_LL location hard-enforces +-90 deg latitude at
         * the C library level, which makes it impossible to also import a
         * tall (>180 row) raw camera image (p.in.pds3's region, north=
         * nrows/south=0) into that same location -- and GRASS rasters from
         * two different locations cannot be mixed in one process without a
         * separate cross-location mechanism this project doesn't use
         * anywhere else (see p.phocube, which keeps the same convention:
         * its own real-data camera-mode tests all run in a PROJECTION_XY
         * location, with computed lat/lon stored as plain cell VALUES,
         * never as the raster's own region). So: run this in a
         * PROJECTION_XY location, with input and output side by side, and
         * set the output region's bounds to whatever real lat/lon box you
         * want, in degrees -- see EXAMPLES. */
        p_spice_init();
        spice_info = read_spice_history(opt_input->answer, input_mapset);
        if (p_spice_str2et(spice_info.time, &et) < 0)
            G_fatal_error(_("SPICE: could not convert time '%s' to ephemeris "
                            "time (is an LSK kernel attached?)."), spice_info.time);

        uppercase_copy(target_upper, sizeof(target_upper), spice_info.target);
        snprintf(fixref, sizeof(fixref), "IAU_%s", target_upper);

        load_iss_camera_model(opt_instrument->answer, opt_input->answer,
                               opt_filter1->answer, opt_filter2->answer,
                               opt_sampling_mode->answer,
                               opt_x_offset->answer, opt_z_offset->answer,
                               opt_swath_width->answer, opt_swath_length->answer,
                               in_cols, in_rows, &cam);

        if (cam.is_omega && !mirror_dn_data)
            G_fatal_error(_("OMEGA_SWIR_C/OMEGA_SWIR_L/OMEGA_VNIR require "
                            "mirror_dn= (the mirror-DN sideplane raster "
                            "written by p.in.pds3 -g for OMEGA cubes)."));

        G_message(_("Camera mode (-c): instrument=%s target=%s observer=%s "
                    "time=%s (et=%.6f)"), opt_instrument->answer, target_upper,
                   spice_info.observer, spice_info.time, et);
    }

    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    DCELL *out_buf = Rast_allocate_d_buf();

    PShapeModel *legacy_shape = NULL;
    if (!camera_mode) {
        legacy_shape = p_shape_ellipsoid(a_km, b_km, c_km);
        if (!legacy_shape)
            G_fatal_error(_("Cannot create ellipsoid shape model"));
    }

    long n_hit = 0, n_miss = 0;

    for (int row = 0; row < out_rows; row++) {
        G_percent(row, out_rows, 2);
        for (int col = 0; col < out_cols; col++) {
            double east  = out_region.west  + (col + 0.5) * out_region.ew_res;
            double north = out_region.north - (row + 0.5) * out_region.ns_res;
            double lat_deg, lon_deg;

            if (proj_sinusoidal) {
                /* Inverse sinusoidal: north=lat (deg), east=(lon-clon)*cos(lat) (deg).
                 * Invalid within +-90 deg latitude, but cos(90)=0 so just clamp. */
                lat_deg = north;
                double coslat = cos(lat_deg * M_PI / 180.0);
                if (fabs(coslat) < 1.0e-10) {
                    lon_deg = clon_deg;
                } else {
                    lon_deg = clon_deg + east / coslat;
                }
            }
            else if (proj_stereo_n || proj_stereo_s) {
                /* Inverse polar stereographic (sphere, true-scale at pole).
                 * rho = sqrt(east^2 + north^2) is tan(co-lat/2) (in degrees).
                 * For North pole: lat = 90 - 2*atan(rho)*R2D, lon = clon + atan2(east,-north)*R2D.
                 * For South pole: lat = -90 + 2*atan(rho)*R2D, lon = clon + atan2(east, north)*R2D.
                 * Here east/north are in degrees (same unit as the forward:
                 * rho_deg = tan(pi/4 - lat/2)*180/pi, so inverse: lat = pi/2-2*atan(rho_deg*pi/180)). */
                double rho_deg = sqrt(east*east + north*north);
                double rho_rad = rho_deg * M_PI / 180.0;
                if (proj_stereo_n) {
                    lat_deg = 90.0 - 2.0 * atan(rho_rad) * 180.0 / M_PI;
                    lon_deg = clon_deg + atan2(east, -north) * 180.0 / M_PI;
                } else {
                    lat_deg = -90.0 + 2.0 * atan(rho_rad) * 180.0 / M_PI;
                    lon_deg = clon_deg + atan2(east, north) * 180.0 / M_PI;
                }
            }
            else {
                /* latlon (default): east=lon, north=lat, no conversion */
                lat_deg = north;
                lon_deg = east;
            }

            /* Clamp to valid lat/lon range (guards against numerical edge cases). */
            if (lat_deg < -90.0 || lat_deg > 90.0) { Rast_set_d_null_value(&out_buf[col], 1); continue; }

            double in_col_f, in_row_f;
            int have_pixel = 1;

            if (camera_mode) {
                double spoint[3];
                if (p_spice_latsrf("Ellipsoid", target_upper, et, fixref,
                                    lon_deg, lat_deg, spoint) < 0) {
                    have_pixel = 0;
                }
                else if (cam.is_vims) {
                    /* VIMS 2-axis angular scan: closed-form inverse.
                     * Single epoch (no per-line timing; VIMS scan is captured
                     * in the look-direction model, not the CK). Two-epoch
                     * rotation matches ISS (fixref at et-lt, cam frame at et). */
                    double pos[3], lt;
                    if (p_spice_pos(target_upper, et, fixref, "LT+S",
                                     spice_info.observer, pos, &lt) < 0) {
                        have_pixel = 0;
                    } else {
                        double ray[3] = { spoint[0]+pos[0], spoint[1]+pos[1],
                                           spoint[2]+pos[2] };
                        double rot_fix2j2000[3][3], rot_j2000_2cam[3][3];
                        if (p_spice_pxform(fixref, "J2000", et-lt,
                                            rot_fix2j2000) < 0 ||
                            p_spice_pxform("J2000", cam.frame, et,
                                            rot_j2000_2cam) < 0) {
                            have_pixel = 0;
                        } else {
                            double inertial[3];
                            for (int i=0;i<3;i++)
                                inertial[i]=rot_fix2j2000[i][0]*ray[0]+
                                             rot_fix2j2000[i][1]*ray[1]+
                                             rot_fix2j2000[i][2]*ray[2];
                            double dvec[3];
                            for (int i=0;i<3;i++)
                                dvec[i]=rot_j2000_2cam[i][0]*inertial[0]+
                                         rot_j2000_2cam[i][1]*inertial[1]+
                                         rot_j2000_2cam[i][2]*inertial[2];

                            /* VimsGroundMap::LookDirection forward:
                             *   v=(sin(θ)cos(φ), cos(θ), -sin(θ)sin(φ))
                             * Inverse: θ=acos(dvec[1]), φ=atan2(-dvec[2],dvec[0])
                             *   x = xBore + (φ+π/2)/xPixSize
                             *   y = yBore + (π/2-θ)/yPixSize
                             *   sample = x - camSampOffset - 1  (0-based)
                             *   line   = y - camLineOffset - 1  (0-based) */
                            double cos_theta = dvec[1];
                            if (cos_theta < -1.0) cos_theta = -1.0;
                            if (cos_theta >  1.0) cos_theta =  1.0;
                            double theta = acos(cos_theta);
                            double phi   = atan2(-dvec[2], dvec[0]);
                            double x = cam.vims_x_bore
                                     + (phi + M_PI/2.0) / cam.vims_x_pixsize;
                            double y = cam.vims_y_bore
                                     + (M_PI/2.0 - theta) / cam.vims_y_pixsize;
                            in_col_f = x - cam.vims_samp_offset - 1.0;
                            in_row_f = y - cam.vims_line_offset - 1.0;
                        }
                    }
                }
                else if (!cam.is_pushbroom) {
                    /* ISS framing camera: single epoch for the whole frame.
                     * Two-epoch rotation (fixref at et-lt, camera frame at et)
                     * is required for bodies far from the spacecraft. */
                    double pos[3], lt;
                    if (p_spice_pos(target_upper, et, fixref, "LT+S",
                                     spice_info.observer, pos, &lt) < 0) {
                        have_pixel = 0;
                    }
                    else {
                        double ray[3] = { spoint[0] + pos[0],
                                           spoint[1] + pos[1],
                                           spoint[2] + pos[2] };
                        double rot_fix2j2000[3][3], rot_j2000_2cam[3][3];
                        if (p_spice_pxform(fixref, "J2000", et - lt,
                                            rot_fix2j2000) < 0 ||
                            p_spice_pxform("J2000", cam.frame, et,
                                            rot_j2000_2cam) < 0) {
                            have_pixel = 0;
                        }
                        else {
                            double inertial[3];
                            for (int i = 0; i < 3; i++)
                                inertial[i] = rot_fix2j2000[i][0]*ray[0] +
                                              rot_fix2j2000[i][1]*ray[1] +
                                              rot_fix2j2000[i][2]*ray[2];
                            double dvec[3];
                            for (int i = 0; i < 3; i++)
                                dvec[i] = rot_j2000_2cam[i][0]*inertial[0] +
                                          rot_j2000_2cam[i][1]*inertial[1] +
                                          rot_j2000_2cam[i][2]*inertial[2];

                            if (dvec[2] <= 0.0) {
                                have_pixel = 0;
                            }
                            else {
                                double t = cam.focal_length / dvec[2];
                                double ux = dvec[0] * t, uy = dvec[1] * t;
                                double dx = ux, dy = uy;
                                for (int it = 0; it < 5; it++) {
                                    double r2 = dx*dx + dy*dy;
                                    double denom = 1.0 + cam.k1 * r2;
                                    dx = ux / denom;
                                    dy = uy / denom;
                                }
                                double sample_1based = cam.boresight_sample + dx / cam.pixel_pitch;
                                double line_1based   = cam.boresight_line   + dy / cam.pixel_pitch;
                                in_col_f = sample_1based - 1.0;
                                in_row_f = line_1based   - 1.0;
                            }
                        }
                    }
                }
                else {
                    /* CRISM/OMEGA pushbroom: each scan line has its own epoch
                     * et_line = et + (line - mid_line) * line_rate.
                     * Outer binary search: find the scan line where the surface
                     * point's along-track component crosses zero.
                     *
                     * CRISM criterion: dvec_mex[1] (Y in MRO_CRISM frame).
                     * OMEGA criterion: dvec_det[1] = Y in OMEGA detector frame
                     *   = omega_rot[:,1] · dvec_mex  (omega_rot^T row 1).
                     *   The OMEGA mirror rotates around Y, so Y is along-track. */
                    if (!spice_info.have_line_rate || spice_info.line_rate == 0.0)
                        G_fatal_error(_("Camera mode (-c) with CRISM/OMEGA requires "
                                        "line_rate in the SPICE history of <%s> "
                                        "(run p.spiceinit with line_rate= attached "
                                        "to the input raster)."),
                                       opt_input->answer);

#define OMEGA_F(dv) (cam.omega_rot[0][1]*(dv)[0] + \
                     cam.omega_rot[1][1]*(dv)[1] + \
                     cam.omega_rot[2][1]*(dv)[2])
/* CTX/LROC_NAC: along-track = X (dvec[0]); CRISM: along-track = Y (dvec[1]) */
#define PUSHBROOM_F(dv) (cam.is_omega ? OMEGA_F(dv) \
    : ((cam.is_ctx || cam.is_lroc_nac) ? (dv)[0] : (dv)[1]))

                    double mid_line_f = (in_rows - 1) / 2.0;
                    int pb_ok = 1;
                    double dvec_lo[3], dvec_hi[3];

                    if (eval_pushbroom_frame(spoint,
                                             et + (0 - mid_line_f) * spice_info.line_rate,
                                             fixref, cam.frame, target_upper,
                                             spice_info.observer, dvec_lo) < 0
                     || eval_pushbroom_frame(spoint,
                                             et + ((in_rows-1) - mid_line_f) * spice_info.line_rate,
                                             fixref, cam.frame, target_upper,
                                             spice_info.observer, dvec_hi) < 0
                     || (dvec_lo[2] <= 0.0 && dvec_hi[2] <= 0.0)
                     || PUSHBROOM_F(dvec_lo) * PUSHBROOM_F(dvec_hi) > 0.0) {
                        pb_ok = 0;
                    }

                    double dvec_best[3] = {0};
                    double in_row_best = 0;

                    if (pb_ok) {
                        int lo = 0, hi = in_rows - 1;
                        double f_lo = PUSHBROOM_F(dvec_lo);
                        memcpy(dvec_best, dvec_lo, sizeof(dvec_lo));

                        for (int iter = 0; iter < 20 && hi - lo > 1; iter++) {
                            int mid = (lo + hi) / 2;
                            double dvec_mid[3];
                            if (eval_pushbroom_frame(spoint,
                                                     et + (mid - mid_line_f) * spice_info.line_rate,
                                                     fixref, cam.frame, target_upper,
                                                     spice_info.observer, dvec_mid) < 0) {
                                pb_ok = 0;
                                break;
                            }
                            double f_mid = PUSHBROOM_F(dvec_mid);
                            if (f_lo * f_mid <= 0.0) {
                                hi = mid;
                                memcpy(dvec_best, dvec_mid, sizeof(dvec_mid));
                            } else {
                                lo = mid;
                                f_lo = f_mid;
                                memcpy(dvec_best, dvec_mid, sizeof(dvec_mid));
                            }
                        }
                        in_row_best = (lo + hi) / 2.0;
                    }
#undef OMEGA_F
#undef PUSHBROOM_F

                    if (!pb_ok || dvec_best[2] <= 0.0) {
                        have_pixel = 0;
                    } else if (cam.is_omega) {
                        /* OMEGA inner sample resolution: rotate dvec_best
                         * (in MEX_SPACECRAFT frame) back to OMEGA detector
                         * frame via omega_rot^T, extract the mirror angle,
                         * invert to get target mirror DN, then binary search
                         * in the mirror_dn sideplane for the matching sample. */
                        int line_int = (int)(in_row_best + 0.5);
                        if (line_int < 0) line_int = 0;
                        if (line_int >= in_rows) line_int = in_rows - 1;

                        double dvec_det[3];
                        for (int i = 0; i < 3; i++)
                            dvec_det[i] = cam.omega_rot[0][i]*dvec_best[0]
                                        + cam.omega_rot[1][i]*dvec_best[1]
                                        + cam.omega_rot[2][i]*dvec_best[2];

                        double theta = atan2(dvec_det[0], dvec_det[2]);
                        double offset_deg = theta * RAD2DEG;
                        double target_dn = offset_deg / cam.mirror_slope
                                         + cam.mirror_center;

                        /* Binary search in mirror_dn_data[line_int][*].
                         * mirror_dn is monotone across samples (mirror sweeps
                         * linearly). Determine the direction from endpoints. */
                        DCELL *mdn_row = mirror_dn_data[line_int];
                        int mlo = 0, mhi = in_cols - 1;
                        int increasing = (mdn_row[0] <= mdn_row[in_cols - 1]);

                        for (int iter = 0; iter < 20 && mhi - mlo > 1; iter++) {
                            int mmid = (mlo + mhi) / 2;
                            if (increasing
                                ? (mdn_row[mmid] < target_dn)
                                : (mdn_row[mmid] > target_dn))
                                mlo = mmid;
                            else
                                mhi = mmid;
                        }

                        /* Interpolate sub-pixel between mlo and mhi */
                        double dn_lo = mdn_row[mlo], dn_hi = mdn_row[mhi];
                        double frac = (dn_hi != dn_lo)
                            ? (target_dn - dn_lo) / (dn_hi - dn_lo) : 0.5;
                        if (frac < 0.0) frac = 0.0;
                        if (frac > 1.0) frac = 1.0;

                        in_col_f = mlo + frac;
                        in_row_f = in_row_best;
                    } else if (cam.is_ctx) {
                        /* CTX cross-track: dvec[1] (Y) is cross-track (sample).
                         * Apply 3-coeff OD_K distortion (ISIS3
                         * SetUndistortedFocalPlane): iteratively find the
                         * distorted position dy from the undistorted uy. */
                        double uy = dvec_best[1] * cam.focal_length / dvec_best[2];
                        double rp = fabs(uy);
                        double rp2 = rp * rp;
                        double drOverR = cam.odk[0] + rp2*(cam.odk[1] + rp2*cam.odk[2]);
                        double r = rp + drOverR * rp;
                        for (int odk_it = 0; odk_it < 15; odk_it++) {
                            double r2 = r * r;
                            double drOverR_new = cam.odk[0] + r2*(cam.odk[1] + r2*cam.odk[2]);
                            double r_new = rp + drOverR_new * r;
                            drOverR = drOverR_new;
                            if (fabs(r_new - r) < cam.pixel_pitch / 100.0) {
                                r = r_new; break;
                            }
                            r = r_new;
                        }
                        double dy = (rp > 0.0) ? (uy / (1.0 - drOverR)) : 0.0;
                        in_col_f = cam.boresight_sample + dy / cam.pixel_pitch - 1.0;
                        in_row_f = in_row_best;
                    } else if (cam.is_lroc_nac) {
                        /* LROC NAC cross-track: dvec[1] (Y) is cross-track.
                         * LroNarrowAngleDistortionMap: uy=dy/(1+K1*dy²), so
                         * inverse (undist→dist): yt = uy*(1+K1*yt²), iterate. */
                        double uy = dvec_best[1] * cam.focal_length / dvec_best[2];
                        double yt = uy;
                        double yprev = 1e30;
                        for (int ni = 0; ni < 50 && fabs(yt - yprev) > 1e-10; ni++) {
                            yprev = yt;
                            yt = uy * (1.0 + cam.nac_k1 * yt * yt);
                        }
                        /* cam.pushbroom_y_sign: +1 for NACL, -1 for NACR */
                        in_col_f = cam.boresight_sample
                                 + cam.pushbroom_y_sign * yt / cam.pixel_pitch - 1.0;
                        in_row_f = in_row_best;
                    } else {
                        /* CRISM/HRSC cross-track: X focal plane, no distortion.
                         * pushbroom_x_sign: +1=CRISM (TRANSX[1]>0),
                         *                  -1=HRSC (TRANSX[1]<0, reversed sample). */
                        double x_fp = dvec_best[0] * cam.focal_length / dvec_best[2];
                        in_col_f = cam.boresight_sample
                                 + cam.pushbroom_x_sign
                                   * (x_fp - cam.focal_plane_x0) / cam.pixel_pitch
                                 - 1.0;
                        in_row_f = in_row_best;
                    }
                }
            }
            else {
                double r_km = p_shape_local_radius_km(legacy_shape, lat_deg, lon_deg);
                double spt[3];
                p_shape_latlon_to_xyz(lat_deg, lon_deg, r_km, spt);
                (void)spt;
                in_col_f = (lon_deg - in_region.west)  / in_region.ew_res - 0.5;
                in_row_f = (in_region.north - lat_deg) / in_region.ns_res - 0.5;
            }

            if (!have_pixel || in_col_f < 0 || in_row_f < 0 ||
                in_col_f > in_cols-1 || in_row_f > in_rows-1) {
                Rast_set_d_null_value(&out_buf[col], 1);
                n_miss++;
                continue;
            }
            n_hit++;

            DCELL val;
            if (!bilinear) {
                int ic = (int)(in_col_f + 0.5);
                int ir = (int)(in_row_f + 0.5);
                if (ic < 0) ic = 0; if (ic >= in_cols) ic = in_cols-1;
                if (ir < 0) ir = 0; if (ir >= in_rows) ir = in_rows-1;
                val = in_data[ir][ic];
            } else {
                int ic0 = (int)in_col_f, ic1 = ic0 + 1;
                int ir0 = (int)in_row_f, ir1 = ir0 + 1;
                if (ic1 >= in_cols) ic1 = in_cols-1;
                if (ir1 >= in_rows) ir1 = in_rows-1;
                double tx = in_col_f - ic0, ty = in_row_f - ir0;
                DCELL v00 = in_data[ir0][ic0], v01 = in_data[ir0][ic1];
                DCELL v10 = in_data[ir1][ic0], v11 = in_data[ir1][ic1];
                if (Rast_is_d_null_value(&v00)||Rast_is_d_null_value(&v01)||
                    Rast_is_d_null_value(&v10)||Rast_is_d_null_value(&v11)) {
                    Rast_set_d_null_value(&out_buf[col], 1);
                    n_miss++;
                    n_hit--;
                    continue;
                }
                val = (DCELL)((1-tx)*(1-ty)*v00 + tx*(1-ty)*v01
                             + (1-tx)*ty*v10 + tx*ty*v11);
            }
            if (Rast_is_d_null_value(&val))
                Rast_set_d_null_value(&out_buf[col], 1);
            else
                out_buf[col] = val;
        }
        Rast_put_d_row(fd_out, out_buf);
    }
    G_percent(1, 1, 2);

    G_message(_("%ld of %ld output pixels hit the input image (%.1f%%)"),
               n_hit, n_hit + n_miss,
               100.0 * n_hit / (double)(n_hit + n_miss));

    for (int r = 0; r < in_rows; r++) G_free(in_data[r]);
    G_free(in_data);
    G_free(out_buf);
    Rast_close(fd_out);

    if (legacy_shape)
        p_shape_free(legacy_shape);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);

    G_message(_("p.cam2map complete: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
