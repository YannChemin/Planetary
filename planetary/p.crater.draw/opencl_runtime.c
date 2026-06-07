/****************************************************************************
 * MODULE:       p.crater.draw (opencl_runtime.c)
 * PURPOSE:      OpenCL runtime: device probe, program compilation, and
 *               GPU kernel dispatch for the DEM and image detectors.
 *
 *               When built with -DHAVE_OPENCL and linked against
 *               -lOpenCL, the kernels in cl_kernels.c are compiled
 *               lazily on first use; subsequent calls re-use the
 *               cached program + kernels.
 *
 *               Without HAVE_OPENCL, dispatch functions compile as
 *               stubs returning NULL so the detectors fall back to
 *               OpenMP transparently.
 *
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/glocale.h>
#include "opencl_runtime.h"
#include "cl_kernels.h"

#ifdef HAVE_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

static char            g_descr[256]      = "";
static int             g_probed          = 0;
static int             g_have_dev        = 0;
static cl_platform_id  g_platform        = 0;
static cl_device_id    g_device          = 0;
static cl_context      g_context         = 0;
static cl_command_queue g_queue          = 0;
static cl_program      g_program_dem     = 0;
static cl_kernel       g_kernel_dem      = 0;
static cl_program      g_program_image   = 0;
static cl_kernel       g_kernel_image    = 0;

int p_crater_draw_opencl_available(int verbose_log)
{
    if (g_probed) return g_have_dev;
    g_probed = 1;

    cl_uint n_platforms = 0;
    if (clGetPlatformIDs(0, NULL, &n_platforms) != CL_SUCCESS ||
        n_platforms == 0) {
        snprintf(g_descr, sizeof(g_descr), "no OpenCL platforms found");
        return 0;
    }
    cl_platform_id plats[8];
    if (n_platforms > 8) n_platforms = 8;
    clGetPlatformIDs(n_platforms, plats, NULL);

    cl_device_type best_type = 0;
    char pname[128] = "", dname[128] = "", dver[128] = "";
    for (cl_uint p = 0; p < n_platforms; p++) {
        cl_uint n_dev = 0;
        if (clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_ALL, 0, NULL, &n_dev)
                != CL_SUCCESS || n_dev == 0) continue;
        cl_device_id devs[8];
        if (n_dev > 8) n_dev = 8;
        clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_ALL, n_dev, devs, NULL);
        for (cl_uint d = 0; d < n_dev; d++) {
            cl_device_type t;
            clGetDeviceInfo(devs[d], CL_DEVICE_TYPE, sizeof(t), &t, NULL);
            cl_device_fp_config fpc = 0;
            clGetDeviceInfo(devs[d], CL_DEVICE_DOUBLE_FP_CONFIG,
                             sizeof(fpc), &fpc, NULL);
            if (fpc == 0) continue;  /* require fp64 */

            if (g_device == 0 ||
                (t == CL_DEVICE_TYPE_GPU && best_type != CL_DEVICE_TYPE_GPU)) {
                g_device   = devs[d];
                g_platform = plats[p];
                best_type  = t;
                clGetPlatformInfo(plats[p], CL_PLATFORM_NAME,
                                    sizeof(pname), pname, NULL);
                clGetDeviceInfo(devs[d], CL_DEVICE_NAME,
                                 sizeof(dname), dname, NULL);
                clGetDeviceInfo(devs[d], CL_DEVICE_VERSION,
                                 sizeof(dver), dver, NULL);
            }
        }
    }
    if (g_device == 0) {
        snprintf(g_descr, sizeof(g_descr),
                 "no OpenCL device with fp64 support found");
        return 0;
    }
    snprintf(g_descr, sizeof(g_descr), "%s on %s (%s)",
             dname, pname, dver);
    if (verbose_log)
        G_message(_("OpenCL: %s"), g_descr);
    g_have_dev = 1;
    return 1;
}

const char *p_crater_draw_opencl_describe(void)
{
    if (g_descr[0] == '\0')
        snprintf(g_descr, sizeof(g_descr),
                 "OpenCL not yet probed (call _available first)");
    return g_descr;
}

static int ensure_context(void)
{
    if (g_context) return 1;
    if (!p_crater_draw_opencl_available(0)) return 0;
    cl_int err;
    g_context = clCreateContext(NULL, 1, &g_device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        G_warning("clCreateContext failed (%d)", err);
        return 0;
    }
    /* Try the OpenCL 2.0 API first; fall back to the deprecated one. */
#ifdef CL_VERSION_2_0
    g_queue = clCreateCommandQueueWithProperties(g_context, g_device,
                                                   NULL, &err);
#else
    err = CL_INVALID_VALUE;
#endif
    if (!g_queue || err != CL_SUCCESS) {
        g_queue = clCreateCommandQueue(g_context, g_device, 0, &err);
        if (err != CL_SUCCESS) {
            G_warning("clCreateCommandQueue failed (%d)", err);
            clReleaseContext(g_context); g_context = 0;
            return 0;
        }
    }
    return 1;
}

static int build_program(const char *src, const char *kernel_name,
                          cl_program *out_prog, cl_kernel *out_kernel)
{
    cl_int err;
    *out_prog = clCreateProgramWithSource(g_context, 1, &src, NULL, &err);
    if (err != CL_SUCCESS) {
        G_warning("clCreateProgramWithSource failed (%d)", err);
        return 0;
    }
    err = clBuildProgram(*out_prog, 1, &g_device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(*out_prog, g_device, CL_PROGRAM_BUILD_LOG,
                                0, NULL, &log_size);
        char *log = malloc(log_size + 1);
        if (log) {
            clGetProgramBuildInfo(*out_prog, g_device, CL_PROGRAM_BUILD_LOG,
                                    log_size, log, NULL);
            log[log_size] = '\0';
            G_warning("OpenCL build log:\n%s", log);
            free(log);
        }
        clReleaseProgram(*out_prog); *out_prog = 0;
        return 0;
    }
    *out_kernel = clCreateKernel(*out_prog, kernel_name, &err);
    if (err != CL_SUCCESS) {
        G_warning("clCreateKernel(%s) failed (%d)", kernel_name, err);
        clReleaseProgram(*out_prog); *out_prog = 0;
        return 0;
    }
    return 1;
}

double *p_crater_draw_cl_run_dem(const double *dem,
                                   int nrows, int ncols,
                                   double radius_px,
                                   int n_az,
                                   const double *cos_az,
                                   const double *sin_az,
                                   double threshold)
{
    if (!ensure_context()) return NULL;
    if (!g_kernel_dem &&
        !build_program(p_crater_draw_cl_dem_src, "detect_dem_kernel",
                        &g_program_dem, &g_kernel_dem)) {
        return NULL;
    }

    cl_int err;
    size_t dem_bytes  = (size_t)nrows * ncols * sizeof(double);
    size_t trig_bytes = (size_t)n_az * sizeof(double);

    cl_mem buf_dem    = clCreateBuffer(g_context,
                            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                            dem_bytes, (void *)dem, &err);
    cl_mem buf_cos_az = clCreateBuffer(g_context,
                            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                            trig_bytes, (void *)cos_az, &err);
    cl_mem buf_sin_az = clCreateBuffer(g_context,
                            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                            trig_bytes, (void *)sin_az, &err);
    cl_mem buf_out    = clCreateBuffer(g_context, CL_MEM_WRITE_ONLY,
                                          dem_bytes, NULL, &err);
    if (!buf_dem || !buf_cos_az || !buf_sin_az || !buf_out) {
        G_warning("clCreateBuffer failed");
        if (buf_dem)    clReleaseMemObject(buf_dem);
        if (buf_cos_az) clReleaseMemObject(buf_cos_az);
        if (buf_sin_az) clReleaseMemObject(buf_sin_az);
        if (buf_out)    clReleaseMemObject(buf_out);
        return NULL;
    }

    clSetKernelArg(g_kernel_dem, 0, sizeof(cl_mem), &buf_dem);
    clSetKernelArg(g_kernel_dem, 1, sizeof(int),    &nrows);
    clSetKernelArg(g_kernel_dem, 2, sizeof(int),    &ncols);
    clSetKernelArg(g_kernel_dem, 3, sizeof(double), &radius_px);
    clSetKernelArg(g_kernel_dem, 4, sizeof(int),    &n_az);
    clSetKernelArg(g_kernel_dem, 5, sizeof(cl_mem), &buf_cos_az);
    clSetKernelArg(g_kernel_dem, 6, sizeof(cl_mem), &buf_sin_az);
    clSetKernelArg(g_kernel_dem, 7, sizeof(double), &threshold);
    clSetKernelArg(g_kernel_dem, 8, sizeof(cl_mem), &buf_out);

    size_t global[2] = { (size_t)ncols, (size_t)nrows };
    err = clEnqueueNDRangeKernel(g_queue, g_kernel_dem, 2, NULL,
                                   global, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        G_warning("clEnqueueNDRangeKernel(dem) failed (%d)", err);
        clReleaseMemObject(buf_dem); clReleaseMemObject(buf_cos_az);
        clReleaseMemObject(buf_sin_az); clReleaseMemObject(buf_out);
        return NULL;
    }
    double *out = G_malloc(dem_bytes);
    err = clEnqueueReadBuffer(g_queue, buf_out, CL_TRUE, 0, dem_bytes,
                                out, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        G_warning("clEnqueueReadBuffer(dem) failed (%d)", err);
        G_free(out); out = NULL;
    }
    clReleaseMemObject(buf_dem); clReleaseMemObject(buf_cos_az);
    clReleaseMemObject(buf_sin_az); clReleaseMemObject(buf_out);
    return out;
}

double *p_crater_draw_cl_run_image(const double *img,
                                     int nrows, int ncols,
                                     double radius_px,
                                     int n_arc,
                                     const double *cos_b,
                                     const double *sin_b,
                                     const double *cos_d,
                                     const double *sin_d,
                                     const double *cos_in,
                                     const double *sin_in,
                                     double threshold)
{
    if (!ensure_context()) return NULL;
    if (!g_kernel_image &&
        !build_program(p_crater_draw_cl_image_src, "detect_image_kernel",
                        &g_program_image, &g_kernel_image)) {
        return NULL;
    }

    cl_int err;
    size_t img_bytes = (size_t)nrows * ncols * sizeof(double);
    size_t arc_bytes = (size_t)n_arc * sizeof(double);
    size_t in_bytes  = 4 * sizeof(double);

    cl_mem buf_img = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        img_bytes, (void *)img, &err);
    cl_mem buf_cb  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        arc_bytes, (void *)cos_b, &err);
    cl_mem buf_sb  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        arc_bytes, (void *)sin_b, &err);
    cl_mem buf_cd  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        arc_bytes, (void *)cos_d, &err);
    cl_mem buf_sd  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        arc_bytes, (void *)sin_d, &err);
    cl_mem buf_ci  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        in_bytes,  (void *)cos_in, &err);
    cl_mem buf_si  = clCreateBuffer(g_context,
                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        in_bytes,  (void *)sin_in, &err);
    cl_mem buf_out = clCreateBuffer(g_context, CL_MEM_WRITE_ONLY,
                                       img_bytes, NULL, &err);
    cl_mem all[] = { buf_img, buf_cb, buf_sb, buf_cd, buf_sd,
                       buf_ci, buf_si, buf_out };
    int n_all = (int)(sizeof(all) / sizeof(all[0]));
    for (int i = 0; i < n_all; i++) {
        if (!all[i]) {
            G_warning("clCreateBuffer (image) failed");
            for (int j = 0; j < n_all; j++) if (all[j]) clReleaseMemObject(all[j]);
            return NULL;
        }
    }

    clSetKernelArg(g_kernel_image,  0, sizeof(cl_mem), &buf_img);
    clSetKernelArg(g_kernel_image,  1, sizeof(int),    &nrows);
    clSetKernelArg(g_kernel_image,  2, sizeof(int),    &ncols);
    clSetKernelArg(g_kernel_image,  3, sizeof(double), &radius_px);
    clSetKernelArg(g_kernel_image,  4, sizeof(int),    &n_arc);
    clSetKernelArg(g_kernel_image,  5, sizeof(cl_mem), &buf_cb);
    clSetKernelArg(g_kernel_image,  6, sizeof(cl_mem), &buf_sb);
    clSetKernelArg(g_kernel_image,  7, sizeof(cl_mem), &buf_cd);
    clSetKernelArg(g_kernel_image,  8, sizeof(cl_mem), &buf_sd);
    clSetKernelArg(g_kernel_image,  9, sizeof(cl_mem), &buf_ci);
    clSetKernelArg(g_kernel_image, 10, sizeof(cl_mem), &buf_si);
    clSetKernelArg(g_kernel_image, 11, sizeof(double), &threshold);
    clSetKernelArg(g_kernel_image, 12, sizeof(cl_mem), &buf_out);

    size_t global[2] = { (size_t)ncols, (size_t)nrows };
    err = clEnqueueNDRangeKernel(g_queue, g_kernel_image, 2, NULL,
                                   global, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        G_warning("clEnqueueNDRangeKernel(image) failed (%d)", err);
        for (int i = 0; i < n_all; i++) clReleaseMemObject(all[i]);
        return NULL;
    }
    double *out = G_malloc(img_bytes);
    err = clEnqueueReadBuffer(g_queue, buf_out, CL_TRUE, 0, img_bytes,
                                out, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        G_warning("clEnqueueReadBuffer(image) failed (%d)", err);
        G_free(out); out = NULL;
    }
    for (int i = 0; i < n_all; i++) clReleaseMemObject(all[i]);
    return out;
}

void p_crater_draw_opencl_shutdown(void)
{
    if (g_kernel_dem)    { clReleaseKernel(g_kernel_dem);     g_kernel_dem    = 0; }
    if (g_kernel_image)  { clReleaseKernel(g_kernel_image);   g_kernel_image  = 0; }
    if (g_program_dem)   { clReleaseProgram(g_program_dem);   g_program_dem   = 0; }
    if (g_program_image) { clReleaseProgram(g_program_image); g_program_image = 0; }
    if (g_queue)         { clReleaseCommandQueue(g_queue);    g_queue         = 0; }
    if (g_context)       { clReleaseContext(g_context);       g_context       = 0; }
}

#else  /* !HAVE_OPENCL */

int p_crater_draw_opencl_available(int verbose_log)
{ (void)verbose_log; return 0; }

const char *p_crater_draw_opencl_describe(void)
{ return "no OpenCL support compiled in (rebuild with -DHAVE_OPENCL and -lOpenCL)"; }

double *p_crater_draw_cl_run_dem(const double *dem, int nr, int nc,
                                   double r, int n_az,
                                   const double *ca, const double *sa,
                                   double t)
{ (void)dem;(void)nr;(void)nc;(void)r;(void)n_az;(void)ca;(void)sa;(void)t;
  return NULL; }

double *p_crater_draw_cl_run_image(const double *img, int nr, int nc,
                                     double r, int n_arc,
                                     const double *cb, const double *sb,
                                     const double *cd, const double *sd,
                                     const double *ci, const double *si,
                                     double t)
{ (void)img;(void)nr;(void)nc;(void)r;(void)n_arc;
  (void)cb;(void)sb;(void)cd;(void)sd;(void)ci;(void)si;(void)t;
  return NULL; }

void p_crater_draw_opencl_shutdown(void) {}

#endif  /* HAVE_OPENCL */
