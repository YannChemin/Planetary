/* psunmask_lib.h — shared-library interface for the OpenMP shadow caster
 *
 * Extracted from p.sunmask so callers can run many shadow casts back-to-back
 * without paying GRASS process startup / raster I/O on every step. The caller
 * owns the DEM and output buffers (typically numpy arrays via ctypes); this
 * function only does the parallel ray-march.
 *
 * Units:
 *   - elev[]   : metres (float32, row-major, nrows × ncols)
 *   - ewres,nsres : metres per pixel (positive)
 *   - alt_deg  : solar altitude above horizon, degrees
 *   - az_deg   : solar azimuth, degrees clockwise from North
 *   - nodata   : value in elev[] that marks NODATA (often FLT_MAX or a sentinel)
 *   - mask[]   : output, one byte per cell: 1=sunlit, 0=shadow, 255=nodata
 *
 * Thread-safety: re-entrant; uses #pragma omp parallel for internally.
 * Honors OMP_NUM_THREADS.
 */
#ifndef PSUNMASK_LIB_H
#define PSUNMASK_LIB_H

#include <stddef.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ── CPU path (always present) ─────────────────────────────────────────── */
void psunmask_cast(const float *elev,
                   int nrows, int ncols,
                   double ewres, double nsres,
                   double alt_deg, double az_deg,
                   float nodata,
                   unsigned char *mask);

/* ── GPU path (OpenCL): persistent context, DEM uploaded once ──────────── */
typedef struct psunmask_gpu_ctx_s psunmask_gpu_ctx_t;

/* On success returns a non-NULL handle and writes a "device-name (NN GiB)"
 * info string into `err` for the caller to log. On failure returns NULL and
 * writes the reason into `err`. Builds without OpenCL always return NULL. */
psunmask_gpu_ctx_t *psunmask_gpu_open(const float *elev,
                                      int nrows, int ncols,
                                      double ewres, double nsres,
                                      float nodata,
                                      char *err, size_t errsz);

/* One shadow cast on a previously-opened context. `mask` must be large enough
 * for nrows*ncols bytes; on return 1=sunlit, 0=shadow, 255=nodata. */
int  psunmask_gpu_cast(psunmask_gpu_ctx_t *ctx,
                       double alt_deg, double az_deg,
                       unsigned char *mask);

void psunmask_gpu_close(psunmask_gpu_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PSUNMASK_LIB_H */
