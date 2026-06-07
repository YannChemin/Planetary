/* backend_opencl.c — OpenCL backend for p.horizon.gpu.
 *
 * Uploads DEM once as image2d_t (CL_R/CL_FLOAT), runs the kernel
 * (horizon_kernel.h) once per azimuth, reads each plane back into the
 * caller's output buffer. One device, one context — keep it simple.
 */
#include "horizon_backend.h"

#ifdef HAVE_OPENCL

#include "horizon_kernel.h"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errsz, const char *fmt, ...) {
    if (!err || errsz == 0) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

static cl_device_id pick_device(cl_int *rc_out, char *err, size_t errsz) {
    cl_uint nplat = 0;
    cl_int rc = clGetPlatformIDs(0, NULL, &nplat);
    if (rc != CL_SUCCESS || nplat == 0) {
        set_err(err, errsz, "no OpenCL platforms (rc=%d)", rc);
        *rc_out = rc;
        return NULL;
    }
    cl_platform_id *plats = calloc(nplat, sizeof(*plats));
    clGetPlatformIDs(nplat, plats, NULL);

    cl_device_id chosen = NULL;
    /* prefer GPU on any platform */
    for (cl_uint i = 0; i < nplat && !chosen; i++) {
        cl_uint nd = 0;
        if (clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, 0, NULL, &nd)
                == CL_SUCCESS && nd > 0) {
            cl_device_id *devs = calloc(nd, sizeof(*devs));
            clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, nd, devs, NULL);
            chosen = devs[0];
            free(devs);
        }
    }
    /* else any device */
    if (!chosen) {
        for (cl_uint i = 0; i < nplat && !chosen; i++) {
            cl_uint nd = 0;
            if (clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_ALL, 0, NULL, &nd)
                    == CL_SUCCESS && nd > 0) {
                cl_device_id *devs = calloc(nd, sizeof(*devs));
                clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_ALL, nd, devs, NULL);
                chosen = devs[0];
                free(devs);
            }
        }
    }
    free(plats);
    if (!chosen) {
        set_err(err, errsz, "no OpenCL devices found");
        *rc_out = -1;
        return NULL;
    }
    *rc_out = CL_SUCCESS;
    return chosen;
}

int horizon_run_ocl(const float *dem,
                    const horizon_params_t *p,
                    const float *rotation_rad,
                    const float *metric_x_row,
                    const float *metric_y_row,
                    const float *az_rad_list, int n_az,
                    float *out_planes,
                    char *errbuf, size_t errsz)
{
    /* If caller passed NULL, supply unit buffers so the kernel can
     * unconditionally sample rotation[]/metric_x[]/metric_y[]. */
    float *rot_zero = NULL;
    float *mx_one = NULL, *my_one = NULL;
    if (!rotation_rad) {
        rot_zero = calloc((size_t)p->nx * p->ny, sizeof(float));
        if (!rot_zero) { set_err(errbuf, errsz, "calloc rot_zero"); return -1; }
        rotation_rad = rot_zero;
    }
    if (!metric_x_row) {
        mx_one = malloc((size_t)p->ny * sizeof(float));
        if (!mx_one) { set_err(errbuf, errsz, "malloc mx_one"); free(rot_zero); return -1; }
        for (int r = 0; r < p->ny; r++) mx_one[r] = 1.0f;
        metric_x_row = mx_one;
    }
    if (!metric_y_row) {
        my_one = malloc((size_t)p->ny * sizeof(float));
        if (!my_one) { set_err(errbuf, errsz, "malloc my_one"); free(rot_zero); free(mx_one); return -1; }
        for (int r = 0; r < p->ny; r++) my_one[r] = 1.0f;
        metric_y_row = my_one;
    }
    /* Projected ray envelope: walk long enough on the SLOWEST metric
     * axis (smallest s) to cover max_dist_m of TRUE geographic distance.
     * Walking K projected metres on axis-s yields K·s true metres ⇒
     * envelope = max_dist_m / min(s). For conformal CRS metric=1 ⇒
     * envelope = max_dist_m (current behaviour preserved). For eqc at
     * latitude φ, s = cos(φ) ⇒ envelope = max_dist_m / cos(φ). Clamp
     * the metric floor at 0.1 to avoid pathological envelopes near the
     * poles (cos⁻¹(0.1) ≈ 84°, beyond which results are unreliable). */
    float min_metric = 1.0f;
    for (int r = 0; r < p->ny; r++) {
        if (metric_x_row[r] > 0.0f && metric_x_row[r] < min_metric)
            min_metric = metric_x_row[r];
        if (metric_y_row[r] > 0.0f && metric_y_row[r] < min_metric)
            min_metric = metric_y_row[r];
    }
    if (min_metric < 0.1f) min_metric = 0.1f;
    const float proj_envelope = p->max_dist_m / min_metric;
    cl_int rc;
    cl_device_id device = pick_device(&rc, errbuf, errsz);
    if (!device) return rc;

    cl_context ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateContext rc=%d", rc); return rc; }
    cl_command_queue q = clCreateCommandQueue(ctx, device, 0, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateCommandQueue rc=%d", rc); goto cleanup_ctx; }

    cl_program prog = clCreateProgramWithSource(ctx, 1, &HORIZON_KERNEL_SRC, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateProgramWithSource rc=%d", rc); goto cleanup_q; }
    rc = clBuildProgram(prog, 1, &device,
                        "-cl-fast-relaxed-math -cl-mad-enable",
                        NULL, NULL);
    if (rc != CL_SUCCESS) {
        size_t logsz = 0;
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &logsz);
        char *log = malloc(logsz + 1);
        if (log) {
            clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, logsz, log, NULL);
            log[logsz] = '\0';
            set_err(errbuf, errsz, "clBuildProgram rc=%d: %s", rc, log);
            free(log);
        } else {
            set_err(errbuf, errsz, "clBuildProgram rc=%d (no log)", rc);
        }
        goto cleanup_prog;
    }
    cl_kernel k = clCreateKernel(prog, "horizon_at_azimuth", &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateKernel rc=%d", rc); goto cleanup_prog; }

    /* DEM as image2d_t */
    cl_image_format fmt;
    fmt.image_channel_order     = CL_R;
    fmt.image_channel_data_type = CL_FLOAT;
    cl_image_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.image_type      = CL_MEM_OBJECT_IMAGE2D;
    desc.image_width     = (size_t)p->nx;
    desc.image_height    = (size_t)p->ny;
    cl_mem dem_img = clCreateImage(ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        &fmt, &desc, (void *)dem, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateImage rc=%d", rc); goto cleanup_k; }

    const size_t plane_bytes  = (size_t)p->nx * (size_t)p->ny * sizeof(float);
    const size_t metric_bytes = (size_t)p->ny * sizeof(float);
    cl_mem rot_buf = clCreateBuffer(ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        plane_bytes, (void *)rotation_rad, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateBuffer rot rc=%d", rc); goto cleanup_img; }
    cl_mem mx_buf = clCreateBuffer(ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        metric_bytes, (void *)metric_x_row, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateBuffer mx rc=%d", rc); goto cleanup_rot; }
    cl_mem my_buf = clCreateBuffer(ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        metric_bytes, (void *)metric_y_row, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateBuffer my rc=%d", rc); goto cleanup_mx; }
    cl_mem out_buf = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, plane_bytes, NULL, &rc);
    if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "clCreateBuffer rc=%d", rc); goto cleanup_my; }

    /* set common args once. Kernel signature order:
     *   dem, rotation, metric_x, metric_y, out, az_rad, cell_m, step_m,
     *   max_dist_m, proj_envelope, inv_2R, nx, ny  */
    clSetKernelArg(k,  0, sizeof(dem_img),  &dem_img);
    clSetKernelArg(k,  1, sizeof(rot_buf),  &rot_buf);
    clSetKernelArg(k,  2, sizeof(mx_buf),   &mx_buf);
    clSetKernelArg(k,  3, sizeof(my_buf),   &my_buf);
    clSetKernelArg(k,  4, sizeof(out_buf),  &out_buf);
    clSetKernelArg(k,  6, sizeof(float),    &p->cell_m);
    clSetKernelArg(k,  7, sizeof(float),    &p->step_m);
    clSetKernelArg(k,  8, sizeof(float),    &p->max_dist_m);
    clSetKernelArg(k,  9, sizeof(float),    &proj_envelope);
    clSetKernelArg(k, 10, sizeof(float),    &p->inv_2R);
    clSetKernelArg(k, 11, sizeof(int),      &p->nx);
    clSetKernelArg(k, 12, sizeof(int),      &p->ny);

    const size_t local[2] = {16, 16};
    const size_t global[2] = {
        ((p->nx + local[0] - 1) / local[0]) * local[0],
        ((p->ny + local[1] - 1) / local[1]) * local[1],
    };

    for (int j = 0; j < n_az; j++) {
        float az = az_rad_list[j];
        clSetKernelArg(k, 5, sizeof(float), &az);
        rc = clEnqueueNDRangeKernel(q, k, 2, NULL, global, local, 0, NULL, NULL);
        if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "NDRange az=%d rc=%d", j, rc); goto cleanup_out; }
        rc = clEnqueueReadBuffer(q, out_buf, CL_TRUE, 0, plane_bytes,
                                 out_planes + (size_t)j * p->nx * p->ny,
                                 0, NULL, NULL);
        if (rc != CL_SUCCESS) { set_err(errbuf, errsz, "ReadBuffer az=%d rc=%d", j, rc); goto cleanup_out; }
    }
    rc = clFinish(q);

cleanup_out: clReleaseMemObject(out_buf);
cleanup_my:  clReleaseMemObject(my_buf);
cleanup_mx:  clReleaseMemObject(mx_buf);
cleanup_rot: clReleaseMemObject(rot_buf);
cleanup_img: clReleaseMemObject(dem_img);
cleanup_k:   clReleaseKernel(k);
cleanup_prog:clReleaseProgram(prog);
cleanup_q:   clReleaseCommandQueue(q);
cleanup_ctx: clReleaseContext(ctx);
    if (rot_zero) free(rot_zero);
    if (mx_one)   free(mx_one);
    if (my_one)   free(my_one);
    return rc;
}

#endif /* HAVE_OPENCL */
