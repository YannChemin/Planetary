/* backend_openmp.c — portable CPU fallback for p.horizon.gpu.
 *
 * Walks the same ray-marching algorithm as horizon_kernel.cl, with
 * manual bilinear sampling. Used when OpenCL is unavailable or the
 * user forces `backend=cpu`.
 */
#include "horizon_backend.h"

#include <math.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Nearest-neighbor sample at fractional pixel coords (fx, fy). Matches
 * r.horizon's cell-centre semantic — bilinear over-smooths sharp
 * crater rims and causes large parity gaps on polar terrain. */
static inline float nearest(const float *dem, int nx, int ny,
                             float fx, float fy, float nodata)
{
    int ix = (int)fx;
    int iy = (int)fy;
    if (ix < 0) ix = 0; else if (ix >= nx) ix = nx - 1;
    if (iy < 0) iy = 0; else if (iy >= ny) iy = ny - 1;
    float v = dem[iy * nx + ix];
    return (v == nodata) ? 0.0f : v;
}

static void horizon_single_az(const float *dem,
                              const horizon_params_t *p,
                              const float *rotation,        /* may be NULL */
                              const float *metric_x_row,    /* may be NULL */
                              const float *metric_y_row,    /* may be NULL */
                              float az_rad,
                              float *out_plane)
{
    const int nx = p->nx, ny = p->ny;
    const float cell_m = p->cell_m;
    const float step_m = p->step_m;
    const float max_d  = p->max_dist_m;
    const float inv_2R = p->inv_2R;
    const float nodata = p->nodata;

    /* Walking K projected metres on an axis with metric s yields K·s
     * TRUE geographic metres. To cover max_d of TRUE distance on the
     * SLOWEST axis (smallest s), we must walk up to max_d / min(s) in
     * projected coords. For conformal CRS metric=1 everywhere ⇒
     * envelope = max_d (no change). For eqc at lat=60° (cos ≈ 0.5),
     * envelope = 2·max_d. We clamp the metric floor at 0.1 (cos ≈ 84°)
     * to avoid pathological envelopes at the poles. */
    float min_metric = 1.0f;
    if (metric_x_row) for (int r = 0; r < ny; r++)
        if (metric_x_row[r] > 0.0f && metric_x_row[r] < min_metric)
            min_metric = metric_x_row[r];
    if (metric_y_row) for (int r = 0; r < ny; r++)
        if (metric_y_row[r] > 0.0f && metric_y_row[r] < min_metric)
            min_metric = metric_y_row[r];
    if (min_metric < 0.1f) min_metric = 0.1f;
    const float proj_envelope = max_d / min_metric;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float z0 = dem[y * nx + x];
            if (z0 == nodata) { out_plane[y * nx + x] = NAN; continue; }
            float rot = rotation ? rotation[y * nx + x] : 0.0f;
            float az_local = az_rad + rot;
            /* CCW-from-(geographic-)east; GRASS row 0 = NORTH ⇒ dy negated. */
            float dx =  cosf(az_local);
            float dy = -sinf(az_local);
            /* r.horizon semantic: one sample per cell crossed, at actual
             * geographic distance from origin pixel centre. */
            float max_e = 0.0f;
            int any = 0, last_ix = x, last_iy = y; /* skip origin naturally */
            for (float s = step_m; s <= proj_envelope; s += step_m) {
                float fx = (float)x + s * dx / cell_m;
                float fy = (float)y + s * dy / cell_m;
                if (fx < 0.0f || fy < 0.0f ||
                    fx >= (float)nx || fy >= (float)ny) break;
                int ix = (int)fx, iy = (int)fy;
                if (ix == last_ix && iy == last_iy) continue;
                last_ix = ix; last_iy = iy;
                /* Anisotropic metric: map projected cell-step (Δix, Δiy)
                 * to TRUE geographic step using per-row metric factors.
                 * For conformal CRS both factors are 1.0 (no change). */
                float sx = metric_x_row ? metric_x_row[iy] : 1.0f;
                float sy = metric_y_row ? metric_y_row[iy] : 1.0f;
                float ddx = (float)(ix - x) * cell_m * sx;
                float ddy = (float)(iy - y) * cell_m * sy;
                float dist = sqrtf(ddx * ddx + ddy * ddy);
                if (dist > max_d) break;
                any = 1;
                float z = nearest(dem, nx, ny, (float)ix, (float)iy, nodata);
                float dz = (z - z0) - dist * dist * inv_2R;
                float elev = atan2f(dz, dist);
                if (elev > max_e) max_e = elev;
            }
            out_plane[y * nx + x] = any ? max_e : NAN;
        }
    }
}

int horizon_run_omp(const float *dem,
                    const horizon_params_t *p,
                    const float *rotation_rad,
                    const float *metric_x_row,
                    const float *metric_y_row,
                    const float *az_rad_list, int n_az,
                    float *out_planes)
{
    const size_t plane = (size_t)p->nx * (size_t)p->ny;
    for (int k = 0; k < n_az; k++) {
        horizon_single_az(dem, p, rotation_rad,
                          metric_x_row, metric_y_row,
                          az_rad_list[k],
                          out_planes + (size_t)k * plane);
    }
    return 0;
}
