/* psunmask_gpu.c — OpenCL backend for psunmask_lib.
 *
 * Persistent OpenCL context: the DEM is uploaded to the device ONCE in
 * psunmask_gpu_open(), and each call to psunmask_gpu_cast() reuses it. The
 * shadow-cast kernel is the same string used by p.sunmask/main.c.
 *
 * Build-time toggle: this file is compiled into libpsunmask.so only when
 * pkg-config finds OpenCL. The header declares the three GPU functions
 * unconditionally; on builds without OpenCL, psunmask_gpu.c is replaced by a
 * tiny stub that makes psunmask_gpu_open() always return NULL.
 *
 * Thread-safety: each context owns its own command queue; callers must not
 * share one context across threads concurrently.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "psunmask_lib.h"

#ifndef HAVE_OPENCL

/* ── stubs for builds without OpenCL ─────────────────────────────────────── */
psunmask_gpu_ctx_t *psunmask_gpu_open(const float *elev,
                                      int nrows, int ncols,
                                      double ewres, double nsres,
                                      float nodata,
                                      char *err, size_t errsz)
{
    (void)elev; (void)nrows; (void)ncols;
    (void)ewres; (void)nsres; (void)nodata;
    if (err && errsz) snprintf(err, errsz, "libpsunmask built without OpenCL");
    return NULL;
}
int psunmask_gpu_cast(psunmask_gpu_ctx_t *ctx,
                      double alt_deg, double az_deg, unsigned char *mask)
{ (void)ctx; (void)alt_deg; (void)az_deg; (void)mask; return -1; }
void psunmask_gpu_close(psunmask_gpu_ctx_t *ctx) { (void)ctx; }

#else  /* HAVE_OPENCL */

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

/* ── kernel source (identical to p.sunmask/main.c) ───────────────────────── */
static const char *KERNEL_SRC =
"__kernel void shadow_cast(\n"
"    __global const float *dem,\n"
"    __global       uchar *out,\n"
"    int   nrows,\n"
"    int   ncols,\n"
"    float nsres,\n"
"    float ewres,\n"
"    float dc,\n"
"    float dr,\n"
"    float tan_alt,\n"
"    float mps,\n"
"    float nodata_val\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= nrows * ncols) return;\n"
"    int r = idx / ncols;\n"
"    int c = idx % ncols;\n"
"    float h0 = dem[r * ncols + c];\n"
"    if (h0 == nodata_val) { out[idx] = 255; return; }\n"
"    uchar in_shadow = 0;\n"
"    float step = 0.5f;\n"
"    float max_steps = (float)(nrows + ncols) * 2.0f;\n"
"    for (float d = step; d <= max_steps; d += step) {\n"
"        float rc = (float)r + d * dr;\n"
"        float cc = (float)c + d * dc;\n"
"        if (rc < 0 || rc >= nrows || cc < 0 || cc >= ncols) break;\n"
"        int r0 = (int)rc;\n"
"        int c0 = (int)cc;\n"
"        int r1 = min(r0 + 1, nrows - 1);\n"
"        int c1 = min(c0 + 1, ncols - 1);\n"
"        float fr = rc - (float)r0;\n"
"        float fc = cc - (float)c0;\n"
"        float h = dem[r0*ncols+c0]*(1.0f-fr)*(1.0f-fc)\n"
"                + dem[r0*ncols+c1]*(1.0f-fr)*fc\n"
"                + dem[r1*ncols+c0]*fr*(1.0f-fc)\n"
"                + dem[r1*ncols+c1]*fr*fc;\n"
"        float dist_m = d * mps;\n"
"        if (h > h0 + dist_m * tan_alt) { in_shadow = 1; break; }\n"
"    }\n"
"    out[idx] = in_shadow ? 0 : 1;\n"
"}\n";

struct psunmask_gpu_ctx_s {
    cl_platform_id   platform;
    cl_device_id     device;
    cl_context       context;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        kernel;
    cl_mem           dem_buf;     /* device-side DEM */
    cl_mem           mask_buf;    /* device-side output mask */
    int              nrows, ncols;
    double           ewres, nsres;
    float            nodata;
    size_t           n_cells;
};

static void set_err(char *err, size_t errsz, const char *fmt, ...) {
    if (!err || errsz == 0) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

psunmask_gpu_ctx_t *psunmask_gpu_open(const float *elev,
                                      int nrows, int ncols,
                                      double ewres, double nsres,
                                      float nodata,
                                      char *err, size_t errsz)
{
    cl_int rc;
    psunmask_gpu_ctx_t *ctx = (psunmask_gpu_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) { set_err(err, errsz, "calloc failed"); return NULL; }

    /* Pick the first platform with at least one GPU. */
    cl_uint nplatforms = 0;
    clGetPlatformIDs(0, NULL, &nplatforms);
    if (nplatforms == 0) { set_err(err, errsz, "no OpenCL platform"); free(ctx); return NULL; }
    cl_platform_id *plats = (cl_platform_id *)malloc(sizeof(cl_platform_id) * nplatforms);
    clGetPlatformIDs(nplatforms, plats, NULL);
    cl_device_id device = 0;
    cl_platform_id platform = 0;
    for (cl_uint p = 0; p < nplatforms; p++) {
        cl_uint ndev = 0;
        if (clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU, 0, NULL, &ndev) != CL_SUCCESS || ndev == 0)
            continue;
        clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU, 1, &device, NULL);
        platform = plats[p];
        break;
    }
    free(plats);
    if (!device) { set_err(err, errsz, "no OpenCL GPU device"); free(ctx); return NULL; }

    /* Memory probe: refuse the device if its largest single allocation can't
     * hold the DEM. */
    size_t dem_bytes  = (size_t)nrows * (size_t)ncols * sizeof(float);
    size_t mask_bytes = (size_t)nrows * (size_t)ncols * sizeof(cl_uchar);
    cl_ulong max_alloc = 0, gmem = 0;
    clGetDeviceInfo(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc), &max_alloc, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE,    sizeof(gmem),      &gmem,      NULL);
    if ((cl_ulong)dem_bytes  > max_alloc ||
        (cl_ulong)mask_bytes > max_alloc ||
        (cl_ulong)(dem_bytes + mask_bytes) > gmem - 64*1024*1024) {
        set_err(err, errsz, "GPU too small: need %zu MiB DEM + %zu MiB mask, "
                "device max_alloc %llu MiB, global %llu MiB",
                dem_bytes/(1024*1024), mask_bytes/(1024*1024),
                (unsigned long long)(max_alloc/(1024*1024)),
                (unsigned long long)(gmem/(1024*1024)));
        free(ctx); return NULL;
    }

    char devname[256] = {0};
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(devname), devname, NULL);

    ctx->platform = platform;
    ctx->device   = device;
    ctx->context  = clCreateContext(NULL, 1, &device, NULL, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateContext: %d", rc); goto fail; }
    ctx->queue    = clCreateCommandQueue(ctx->context, device, 0, &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateCommandQueue: %d", rc); goto fail; }

    ctx->program  = clCreateProgramWithSource(ctx->context, 1, &KERNEL_SRC, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateProgramWithSource: %d", rc); goto fail; }
    rc = clBuildProgram(ctx->program, 1, &device, "-cl-fast-relaxed-math", NULL, NULL);
    if (rc != CL_SUCCESS) {
        size_t logsz = 0;
        clGetProgramBuildInfo(ctx->program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &logsz);
        char *log = (char *)malloc(logsz + 1);
        if (log) {
            clGetProgramBuildInfo(ctx->program, device, CL_PROGRAM_BUILD_LOG, logsz, log, NULL);
            log[logsz] = 0;
            set_err(err, errsz, "clBuildProgram: %d — %s", rc, log);
            free(log);
        } else {
            set_err(err, errsz, "clBuildProgram: %d (no log)", rc);
        }
        goto fail;
    }

    ctx->kernel   = clCreateKernel(ctx->program, "shadow_cast", &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateKernel: %d", rc); goto fail; }

    ctx->dem_buf  = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                    dem_bytes, (void *)elev, &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateBuffer dem: %d", rc); goto fail; }
    ctx->mask_buf = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, mask_bytes, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(err, errsz, "clCreateBuffer mask: %d", rc); goto fail; }

    ctx->nrows = nrows; ctx->ncols = ncols;
    ctx->ewres = ewres; ctx->nsres = nsres;
    ctx->nodata = nodata;
    ctx->n_cells = (size_t)nrows * (size_t)ncols;

    set_err(err, errsz, "%s (%.1f GiB VRAM, %llu MiB max alloc)", devname,
            gmem / (1024.0*1024.0*1024.0),
            (unsigned long long)(max_alloc/(1024*1024)));
    return ctx;

fail:
    psunmask_gpu_close(ctx);
    return NULL;
}

int psunmask_gpu_cast(psunmask_gpu_ctx_t *ctx,
                      double alt_deg, double az_deg,
                      unsigned char *mask)
{
    if (!ctx) return -1;
    const double DEG = M_PI / 180.0;
    double alt = alt_deg * DEG;
    double az  = az_deg  * DEG;
    size_t mask_bytes = ctx->n_cells * sizeof(cl_uchar);

    /* Sun below horizon: every cell in shadow; skip the kernel entirely. */
    if (alt <= 0.0) {
        memset(mask, 0, mask_bytes);
        return 0;
    }

    float dc =  (float)sin(az);
    float dr = -(float)cos(az);
    float mps = (float)sqrt(fabs(dr)*ctx->nsres*fabs(dr)*ctx->nsres +
                            fabs(dc)*ctx->ewres*fabs(dc)*ctx->ewres);
    if (mps == 0.0f) mps = (float)((ctx->nsres + ctx->ewres) * 0.5);
    float tan_alt = (float)tan(alt);
    float nodata  = ctx->nodata;

    cl_int rc;
    int ai = 0;
    rc  = clSetKernelArg(ctx->kernel, ai++, sizeof(cl_mem),   &ctx->dem_buf);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_mem),   &ctx->mask_buf);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_int),   &ctx->nrows);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_int),   &ctx->ncols);
    float nsresf = (float)ctx->nsres, ewresf = (float)ctx->ewres;
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &nsresf);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &ewresf);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &dc);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &dr);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &tan_alt);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &mps);
    rc |= clSetKernelArg(ctx->kernel, ai++, sizeof(cl_float), &nodata);
    if (rc != CL_SUCCESS) return -2;

    size_t global = ctx->n_cells;
    rc = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel, 1, NULL, &global, NULL, 0, NULL, NULL);
    if (rc != CL_SUCCESS) return -3;

    rc = clEnqueueReadBuffer(ctx->queue, ctx->mask_buf, CL_TRUE, 0, mask_bytes, mask,
                             0, NULL, NULL);
    if (rc != CL_SUCCESS) return -4;

    return 0;
}

void psunmask_gpu_close(psunmask_gpu_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->mask_buf) clReleaseMemObject(ctx->mask_buf);
    if (ctx->dem_buf)  clReleaseMemObject(ctx->dem_buf);
    if (ctx->kernel)   clReleaseKernel(ctx->kernel);
    if (ctx->program)  clReleaseProgram(ctx->program);
    if (ctx->queue)    clReleaseCommandQueue(ctx->queue);
    if (ctx->context)  clReleaseContext(ctx->context);
    free(ctx);
}

#endif /* HAVE_OPENCL */
