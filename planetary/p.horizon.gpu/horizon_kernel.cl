/* horizon_kernel.cl
 *
 * Per-pixel horizon elevation at a single azimuth, computed by ray-marching
 * a DEM uploaded as a 2D image (hardware bilinear sampling via texture
 * cache). One kernel launch per azimuth; one work-item per output pixel.
 *
 * Inputs (set by host before each launch):
 *   dem          image2d_t, CL_R / CL_FLOAT, normalized=false
 *   out          __global float* (length nx*ny)
 *   az_rad       float, azimuth in radians (CCW from east; see note below)
 *   cell_m       float, DEM cell size in metres (planar)
 *   step_m       float, ray step in metres (default 0.5 * cell_m)
 *   max_dist_m   float, ray cap in metres
 *   inv_2R       float, 1 / (2 * body_radius_m), curvature term
 *   nx, ny       int, DEM dimensions in cells
 *
 * Output: horizon[y*nx + x] = max apparent elevation (radians) along the
 * ray from pixel (x,y) at azimuth az_rad, or NAN if no sample was taken
 * (i.e. ray exited the DEM at the first step).
 *
 * Azimuth convention: r.horizon uses CCW-from-east; we match that so
 * downstream interpolate_horizon() stays unchanged. dx = cos(az), dy =
 * sin(az), with +y pointing north (image row direction is inverted by
 * the host when uploading the DEM).
 */

/* Nearest-neighbor sampling to match r.horizon's cell-center semantic.
 * Bilinear (CLK_FILTER_LINEAR) over-smooths sharp crater rims compared
 * to r.horizon's "walk cell-by-cell, sample cell centre" behaviour and
 * causes large per-pixel disagreements on polar terrain. Texture cache
 * still wins us locality even with nearest. */
__constant sampler_t NEAR =
    CLK_NORMALIZED_COORDS_FALSE |
    CLK_ADDRESS_CLAMP_TO_EDGE   |
    CLK_FILTER_NEAREST;

__kernel void horizon_at_azimuth(
    __read_only image2d_t dem,
    __global    const float *rotation,    /* per-pixel az offset, radians; may be all zeros */
    __global    const float *metric_x,    /* per-row east-metric multiplier (ny floats); all 1.0 → isotropic */
    __global    const float *metric_y,    /* per-row north-metric multiplier */
    __global          float *out,
    const float az_rad,
    const float cell_m,
    const float step_m,
    const float max_dist_m,
    const float proj_envelope,            /* projected ray cap; covers max_dist_m of TRUE distance even at worst metric */
    const float inv_2R,
    const int   nx,
    const int   ny)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    if (x >= nx || y >= ny) return;

    /* Origin sample. Texture coordinates are pixel-centred (+0.5). */
    const float z0 = read_imagef(dem, NEAR, (float2)(x + 0.5f, y + 0.5f)).x;

    /* Apply per-pixel rotation so the ray walks toward geographic-az
     * regardless of projection distortion (e.g. polar stereographic). */
    const float az_local = az_rad + rotation[y * nx + x];

    /* CCW-from-(geographic-)east azimuth. GRASS row 0 is NORTH, so going
     * north means decreasing row → dy negated relative to math-y-up frame. */
    const float dx =  cos(az_local);
    const float dy = -sin(az_local);

    /* r.horizon semantic: walk in fixed sub-cell steps, but only sample
     * a cell the FIRST time the ray enters it, and use the actual
     * geographic distance from the origin pixel to that cell's centre. */
    float max_elev = 0.0f;
    int   any_sample = 0;
    int   last_ix = x, last_iy = y;  /* origin skipped naturally */

    for (float s = step_m; s <= proj_envelope; s += step_m) {
        float fx = (float)x + s * dx / cell_m;
        float fy = (float)y + s * dy / cell_m;
        if (fx < 0.0f || fy < 0.0f || fx >= (float)nx || fy >= (float)ny)
            break;
        int ix = (int)fx;
        int iy = (int)fy;
        if (ix == last_ix && iy == last_iy) continue;
        last_ix = ix; last_iy = iy;
        /* Anisotropic metric: scale projected cell-steps to TRUE
         * geographic metres using per-row factors. For conformal CRS
         * both factors are 1.0 and this reduces to the original
         * isotropic formula. For equirectangular, metric_x[iy] =
         * 1/cos(lat_iy) and metric_y[iy] = 1.0. */
        float sx = metric_x[iy];
        float sy = metric_y[iy];
        float ddx = (float)(ix - x) * cell_m * sx;
        float ddy = (float)(iy - y) * cell_m * sy;
        float dist = sqrt(ddx * ddx + ddy * ddy);
        if (dist > max_dist_m) break;
        any_sample = 1;
        float z = read_imagef(dem, NEAR, (int2)(ix, iy)).x;
        float dz = (z - z0) - dist * dist * inv_2R;
        float elev = atan2(dz, dist);
        if (elev > max_elev) max_elev = elev;
    }

    out[y * nx + x] = any_sample ? max_elev : NAN;
}
