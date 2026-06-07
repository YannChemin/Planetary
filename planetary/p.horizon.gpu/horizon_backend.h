/* horizon_backend.h — backend interface for p.horizon.gpu.
 *
 * Both OpenCL and OpenMP backends implement the same signature:
 * given a DEM and an azimuth list, fill one output plane per azimuth.
 * Output values are elevation in radians; NaN means "no sample" (ray
 * exited the DEM at the first step).
 */
#ifndef P_HORIZON_GPU_BACKEND_H
#define P_HORIZON_GPU_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common ray-marching parameters. */
typedef struct {
    int    nx, ny;          /* DEM width/height in cells */
    float  cell_m;           /* planar cell size in metres (square cells) */
    float  step_m;           /* ray step in metres */
    float  max_dist_m;       /* ray cap in metres */
    float  inv_2R;           /* 1 / (2 * body_radius_m) for curvature */
    float  nodata;           /* DEM nodata sentinel (e.g. -FLT_MAX); treated as ground */
} horizon_params_t;

/* OpenMP backend (always available). Returns 0 on success.
 *
 * `rotation_rad` is an optional per-pixel rotation (length nx*ny):
 * the angle, in radians, between projected +x (column-increasing) and
 * geographic east at that pixel. The kernel walks the ray in direction
 * (az_rad + rotation[pixel]) so that az=0 always means "toward
 * geographic east" regardless of projection distortion. Pass NULL for
 * Cartesian / equirectangular projections (no rotation needed).
 *
 * `metric_x_row`, `metric_y_row` are optional per-row anisotropic metric
 * factors (length ny each), giving the true geographic distance per unit
 * of projected step on the east and north axes at each DEM row. Both
 * NULL → isotropic metric (true distance == projected distance × cell_m),
 * which is the correct behaviour for conformal CRS where the cell size
 * already encodes the local metric. For equirectangular CRS the east
 * factor is 1/cos(lat_row) (and north stays 1.0), restoring geographic-
 * faithful ray-distance, curvature correction and elevation angle. */
int horizon_run_omp(const float *dem,
                    const horizon_params_t *p,
                    const float *rotation_rad,    /* NULL → zero rotation */
                    const float *metric_x_row,    /* NULL → 1.0 per row */
                    const float *metric_y_row,    /* NULL → 1.0 per row */
                    const float *az_rad_list, int n_az,
                    float *out_planes);  /* [n_az * nx * ny] */

#ifdef HAVE_OPENCL
/* OpenCL backend. Returns 0 on success, non-zero if OpenCL setup
 * failed (caller should fall back to OMP). Diagnostic message is
 * written to errbuf if errsz > 0. */
int horizon_run_ocl(const float *dem,
                    const horizon_params_t *p,
                    const float *rotation_rad,    /* NULL → zero rotation */
                    const float *metric_x_row,    /* NULL → 1.0 per row */
                    const float *metric_y_row,    /* NULL → 1.0 per row */
                    const float *az_rad_list, int n_az,
                    float *out_planes,
                    char *errbuf, size_t errsz);
#endif

#ifdef __cplusplus
}
#endif

#endif /* P_HORIZON_GPU_BACKEND_H */
