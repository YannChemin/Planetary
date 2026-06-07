/* psunmask_lib.c — OpenMP shadow caster, library form.
 *
 * Same algorithm as p.sunmask's shadow_omp() in main.c. Extracted here so the
 * function can be called repeatedly (e.g. by p.illumination.sunfraction.fast)
 * without GRASS process startup or per-step raster I/O. Keep the two
 * implementations in sync.
 */
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "psunmask_lib.h"

static inline float bilerp(const float *dem, int nrows, int ncols,
                           double rc, double cc, float nodata)
{
    int r0 = (int)rc, c0 = (int)cc;
    int r1 = r0 + 1 < nrows ? r0 + 1 : r0;
    int c1 = c0 + 1 < ncols ? c0 + 1 : c0;
    double fr = rc - r0, fc = cc - c0;
    float v00 = dem[r0*ncols+c0], v01 = dem[r0*ncols+c1];
    float v10 = dem[r1*ncols+c0], v11 = dem[r1*ncols+c1];
    if (v00 == nodata || v01 == nodata || v10 == nodata || v11 == nodata)
        return nodata;
    return (float)((1-fr)*(1-fc)*v00 + (1-fr)*fc*v01
                 + fr*(1-fc)*v10 + fr*fc*v11);
}

void psunmask_cast(const float *elev,
                   int nrows, int ncols,
                   double ewres, double nsres,
                   double alt_deg, double az_deg,
                   float nodata,
                   unsigned char *mask)
{
    const double DEG = 3.14159265358979323846 / 180.0;
    double alt = alt_deg * DEG;
    double az  = az_deg  * DEG;          /* compass bearing CW from North */

    /* If the sun is below the geometric horizon, every pixel is in shadow. */
    if (alt <= 0.0) {
        long long n = (long long)nrows * (long long)ncols;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (long long i = 0; i < n; i++) {
            mask[i] = (elev[i] == nodata) ? 255 : 0;
        }
        return;
    }

    /* Ray direction in grid coordinates. Image rows grow southward.
     * dr (rows per ray-step) is negative when the sun is to the north.
     * The metres-per-grid-step factor (mps) converts d (in pixel units) to
     * horizontal distance for the height-required test. */
    double dc =  sin(az);                /* east  component (cols / unit) */
    double dr = -cos(az);                /* south component (rows / unit), N=0 -> -1 */
    /* Average pixel size: shadow ray step is 0.5 pixels in (dr,dc) space, so
     * the horizontal distance per unit-d is the geometric mean of nsres/ewres
     * weighted by the ray's components. Using the simple mean keeps the test
     * conservative; this matches p.sunmask's shadow_omp. */
    double mps = sqrt(fabs(dr)*nsres*fabs(dr)*nsres + fabs(dc)*ewres*fabs(dc)*ewres);
    if (mps == 0.0) mps = (nsres + ewres) * 0.5;
    double tan_alt = tan(alt);

    double max_steps = (double)(nrows + ncols) * 2.0;
    double step = 0.5;

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic, 32)
    #endif
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            float h0 = elev[(long long)r * ncols + c];
            if (h0 == nodata) {
                mask[(long long)r * ncols + c] = 255;
                continue;
            }
            int in_shadow = 0;
            for (double d = step; d <= max_steps; d += step) {
                double rc = r + d * dr;
                double cc = c + d * dc;
                if (rc < 0 || rc >= nrows || cc < 0 || cc >= ncols)
                    break;
                float h = bilerp(elev, nrows, ncols, rc, cc, nodata);
                if (h == nodata) break;
                double dist_m = d * mps;
                if (h > h0 + dist_m * tan_alt) {
                    in_shadow = 1;
                    break;
                }
            }
            mask[(long long)r * ncols + c] = in_shadow ? 0 : 1;
        }
    }
}
