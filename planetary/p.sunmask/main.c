/****************************************************************************
 * MODULE:       p.sunmask
 * PURPOSE:      Shadow mask from DEM + sun position.
 *               Drop-in replacement for r.sunmask (altitude= / azimuth= mode)
 *               with CPU/GPU parallelism:
 *                 - Always: OpenMP parallel for over rows (CPU multi-core).
 *                 - Optional: OpenCL kernel on any OpenCL device (GPU or CPU).
 *               OpenCL is probed at runtime; falls back silently to OpenMP
 *               if no platform/device is found or if the build was made
 *               without HAVE_OPENCL.
 *
 * AUTHOR(S):    Yann Chemin
 * LICENSE:      Unlicense (https://unlicense.org)
 *
 * Algorithm:
 *   For each output pixel P at (row r, col c) with elevation h0:
 *     Cast a ray from P toward the sun (azimuth, altitude).
 *     Walk along the ray in steps of 0.5 pixels.
 *     At each sampled point Q (bilinear interp from DEM):
 *       required_height = h0 + horizontal_dist_m * tan(altitude_rad)
 *       if DEM(Q) > required_height → P is in shadow → output 0
 *     If no occlusion found → P is sunlit → output 1
 *     Nodata input cells → nodata output.
 *
 * Output: 1=sunlit, 0=shadowed, NULL=nodata (matches r.sunmask after
 *         r.null null=0, with same convention as p.illumination.sunfraction).
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef HAVE_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* ── OpenCL kernel source (embedded string) ─────────────────────────────── */

static const char *OCL_KERNEL_SRC =
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
"    float mps,\n"       /* metres per step (precomputed) */
"    float nodata_val\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= nrows * ncols) return;\n"
"    int r = idx / ncols;\n"
"    int c = idx % ncols;\n"
"    float h0 = dem[r * ncols + c];\n"
"    if (h0 == nodata_val) { out[idx] = 255; return; }\n"  /* 255 = nodata marker */
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

/* ── helper: bilinear sample from float array ────────────────────────────── */

static inline float bilerp(const float *dem, int nrows, int ncols,
                            double rc, double cc, float nodata)
{
    int r0 = (int)rc, c0 = (int)cc;
    int r1 = r0 + 1 < nrows ? r0 + 1 : r0;
    int c1 = c0 + 1 < ncols ? c0 + 1 : c0;
    double fr = rc - r0, fc = cc - c0;
    float v00 = dem[r0*ncols+c0], v01 = dem[r0*ncols+c1];
    float v10 = dem[r1*ncols+c0], v11 = dem[r1*ncols+c1];
    /* propagate nodata */
    if (v00 == nodata || v01 == nodata || v10 == nodata || v11 == nodata)
        return nodata;
    return (float)((1-fr)*(1-fc)*v00 + (1-fr)*fc*v01
                 + fr*(1-fc)*v10 + fr*fc*v11);
}

/* ── OpenMP shadow computation ───────────────────────────────────────────── */

static void shadow_omp(const float *dem, unsigned char *out,
                       int nrows, int ncols,
                       double dc, double dr, double tan_alt, double mps,
                       float nodata)
{
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            float h0 = dem[r * ncols + c];
            if (h0 == nodata) {
                out[r * ncols + c] = 255;
                continue;
            }
            int in_shadow = 0;
            double step = 0.5;
            double max_steps = (nrows + ncols) * 2.0;
            for (double d = step; d <= max_steps; d += step) {
                double rc = r + d * dr;
                double cc = c + d * dc;
                if (rc < 0 || rc >= nrows || cc < 0 || cc >= ncols)
                    break;
                float h = bilerp(dem, nrows, ncols, rc, cc, nodata);
                if (h == nodata) break;
                double dist_m = d * mps;
                if (h > h0 + dist_m * tan_alt) {
                    in_shadow = 1;
                    break;
                }
            }
            out[r * ncols + c] = in_shadow ? 0 : 1;
        }
    }
}

/* ── OpenCL shadow computation ───────────────────────────────────────────── */

#ifdef HAVE_OPENCL
static int shadow_ocl(const float *dem, unsigned char *out,
                      int nrows, int ncols,
                      double dc, double dr, double tan_alt, double mps,
                      float nodata)
{
    cl_int err;
    cl_uint nplatforms = 0;
    cl_platform_id platform;

    /* probe platforms */
    clGetPlatformIDs(0, NULL, &nplatforms);
    if (nplatforms == 0) return 0;  /* no OpenCL → signal fallback */
    clGetPlatformIDs(1, &platform, NULL);

    /* prefer GPU, accept CPU */
    cl_device_id device;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS)
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, NULL);
    if (err != CL_SUCCESS) return 0;

    char dev_name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
    G_message(_("OpenCL device: %s"), dev_name);

    cl_context ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return 0;

    cl_command_queue queue = clCreateCommandQueue(ctx, device, 0, &err);
    if (err != CL_SUCCESS) { clReleaseContext(ctx); return 0; }

    /* compile kernel */
    cl_program prog = clCreateProgramWithSource(ctx, 1, &OCL_KERNEL_SRC,
                                                NULL, &err);
    err = clBuildProgram(prog, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_sz;
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_sz);
        char *log = G_malloc(log_sz);
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, log_sz, log, NULL);
        G_warning(_("OpenCL build failed:\n%s"), log);
        G_free(log);
        clReleaseProgram(prog);
        clReleaseCommandQueue(queue);
        clReleaseContext(ctx);
        return 0;
    }

    cl_kernel kernel = clCreateKernel(prog, "shadow_cast", &err);
    if (err != CL_SUCCESS) {
        clReleaseProgram(prog); clReleaseCommandQueue(queue);
        clReleaseContext(ctx); return 0;
    }

    size_t n = (size_t)nrows * ncols;

    /* device buffers */
    cl_mem d_dem = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  n * sizeof(float), (void *)dem, &err);
    cl_mem d_out = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                  n * sizeof(unsigned char), NULL, &err);

    /* kernel arguments */
    float f_dc = (float)dc, f_dr = (float)dr;
    float f_tan = (float)tan_alt, f_mps = (float)mps;
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_dem);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_out);
    clSetKernelArg(kernel, 2, sizeof(cl_int), &nrows);
    clSetKernelArg(kernel, 3, sizeof(cl_int), &ncols);
    float f_nsres = 1.0f, f_ewres = 1.0f;  /* absorbed into mps */
    clSetKernelArg(kernel, 4, sizeof(cl_float), &f_nsres);
    clSetKernelArg(kernel, 5, sizeof(cl_float), &f_ewres);
    clSetKernelArg(kernel, 6, sizeof(cl_float), &f_dc);
    clSetKernelArg(kernel, 7, sizeof(cl_float), &f_dr);
    clSetKernelArg(kernel, 8, sizeof(cl_float), &f_tan);
    clSetKernelArg(kernel, 9, sizeof(cl_float), &f_mps);
    clSetKernelArg(kernel, 10, sizeof(cl_float), &nodata);

    /* run */
    size_t global = n;
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, NULL,
                                 0, NULL, NULL);
    clFinish(queue);

    /* read back */
    clEnqueueReadBuffer(queue, d_out, CL_TRUE, 0,
                        n * sizeof(unsigned char), out, 0, NULL, NULL);

    clReleaseMemObject(d_dem);
    clReleaseMemObject(d_out);
    clReleaseKernel(kernel);
    clReleaseProgram(prog);
    clReleaseCommandQueue(queue);
    clReleaseContext(ctx);
    return 1;  /* success */
}
#endif  /* HAVE_OPENCL */

/* ── GRASS module entry point ────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_elev, *opt_out, *opt_alt, *opt_az;
    struct Flag    *flag_cpu;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Illumination"));
    G_add_keyword(_("shadow"));
    G_add_keyword(_("solar"));
    G_add_keyword(_("OpenMP"));
    G_add_keyword(_("OpenCL"));
    module->description = _("Shadow mask from DEM and sun position "
                             "(OpenMP + optional OpenCL acceleration).");

    opt_elev = G_define_standard_option(G_OPT_R_INPUT);
    opt_elev->key         = "elevation";
    opt_elev->description = _("Name of input elevation raster (metres)");

    opt_out = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_out->description  = _("Name for output shadow mask raster "
                               "(1=sunlit, 0=shadow, NULL=nodata)");

    opt_alt = G_define_option();
    opt_alt->key          = "altitude";
    opt_alt->type         = TYPE_DOUBLE;
    opt_alt->required     = YES;
    opt_alt->options      = "0-89.999";
    opt_alt->description  = _("Sun altitude above horizon in degrees");

    opt_az = G_define_option();
    opt_az->key           = "azimuth";
    opt_az->type          = TYPE_DOUBLE;
    opt_az->required      = YES;
    opt_az->options       = "0-360";
    opt_az->description   = _("Sun azimuth in degrees (0=N, 90=E, 180=S, 270=W)");

    flag_cpu = G_define_flag();
    flag_cpu->key         = 'c';
    flag_cpu->description = _("Force CPU (OpenMP) even if OpenCL is available");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    double altitude = atof(opt_alt->answer);
    double azimuth  = atof(opt_az->answer);
    int    force_cpu = flag_cpu->answer;

    if (altitude <= 0.0)
        G_fatal_error(_("altitude must be > 0° (sun below horizon casts no shadow)"));

    /* ── region info ─────────────────────────────────────────────────── */
    struct Cell_head window;
    G_get_window(&window);
    int nrows = window.rows;
    int ncols = window.cols;
    double nsres = window.ns_res;
    double ewres = window.ew_res;

    G_message(_("Region: %d rows × %d cols  (%.2f m × %.2f m pixels)"),
              nrows, ncols, nsres, ewres);
    G_message(_("Sun: altitude=%.3f°  azimuth=%.3f°"), altitude, azimuth);

    /* ── ray direction in pixel space ─────────────────────────────────── */
    double az_rad  = azimuth  * M_PI / 180.0;
    double alt_rad = altitude * M_PI / 180.0;
    double tan_alt = tan(alt_rad);

    /* Column and row increments for a unit step toward the sun.
       In raster space: row 0=North, col 0=West; positive row goes South. */
    double dc =  sin(az_rad);   /* east-positive column increment */
    double dr = -cos(az_rad);   /* row increment (north = negative row) */

    /* Horizontal distance in metres per unit pixel step along the ray */
    double mps = sqrt((dc * ewres) * (dc * ewres) + (dr * nsres) * (dr * nsres));
    if (mps < 1e-9)
        G_fatal_error(_("Degenerate ray direction; check azimuth"));

    /* ── read DEM into flat float array ──────────────────────────────── */
    G_message(_("Loading DEM into memory…"));
    int fd_in = Rast_open_old(opt_elev->answer, "");
    FCELL  *row_buf = Rast_allocate_f_buf();
    float  *dem     = G_malloc((size_t)nrows * ncols * sizeof(float));

    float nodata = -FLT_MAX;  /* sentinel for GRASS null */

    for (int r = 0; r < nrows; r++) {
        Rast_get_f_row(fd_in, row_buf, r);
        for (int c = 0; c < ncols; c++) {
            if (Rast_is_f_null_value(&row_buf[c]))
                dem[r * ncols + c] = nodata;
            else
                dem[r * ncols + c] = row_buf[c];
        }
        G_percent(r, nrows, 5);
    }
    G_percent(nrows, nrows, 5);
    Rast_close(fd_in);
    G_free(row_buf);

    /* ── allocate output array ────────────────────────────────────────── */
    unsigned char *out = G_malloc((size_t)nrows * ncols * sizeof(unsigned char));

    /* ── run shadow computation ───────────────────────────────────────── */
    int used_ocl = 0;

#ifdef HAVE_OPENCL
    if (!force_cpu) {
        G_message(_("Trying OpenCL…"));
        used_ocl = shadow_ocl(dem, out, nrows, ncols,
                              dc, dr, tan_alt, mps, nodata);
        if (used_ocl)
            G_message(_("Shadow computation: OpenCL"));
        else
            G_message(_("OpenCL unavailable or failed — falling back to OpenMP"));
    }
#endif

    if (!used_ocl) {
#ifdef _OPENMP
        G_message(_("Shadow computation: OpenMP (%d thread(s))"),
                  omp_get_max_threads());
#else
        G_message(_("Shadow computation: single-threaded (no OpenMP)"));
#endif
        shadow_omp(dem, out, nrows, ncols,
                   dc, dr, tan_alt, mps, nodata);
    }

    G_free(dem);

    /* ── write output raster ─────────────────────────────────────────── */
    G_message(_("Writing output raster…"));
    int fd_out = Rast_open_new(opt_out->answer, CELL_TYPE);
    CELL *out_row = Rast_allocate_c_buf();

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            unsigned char v = out[r * ncols + c];
            if (v == 255)
                Rast_set_c_null_value(&out_row[c], 1);
            else
                out_row[c] = (CELL)v;
        }
        Rast_put_c_row(fd_out, out_row);
    }
    Rast_close(fd_out);
    G_free(out_row);
    G_free(out);

    /* metadata */
    struct Colors colors;
    Rast_init_colors(&colors);
    CELL zero = 0, one = 1;
    Rast_add_c_color_rule(&zero, 0, 0, 0,
                           &one,  255, 255, 255, &colors);
    Rast_write_colors(opt_out->answer, G_mapset(), &colors);
    Rast_free_colors(&colors);

    struct History hist;
    Rast_short_history(opt_out->answer, "raster", &hist);
    Rast_command_history(&hist);
    Rast_write_history(opt_out->answer, &hist);

    G_done_msg(_("Map <%s> created (1=sunlit, 0=shadow)."), opt_out->answer);
    return EXIT_SUCCESS;
}
