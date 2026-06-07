# p_shapemodel Library

Planetary body shape models for ray-surface intersection and ray-sphere geometry calculations.

## Overview

The p_shapemodel library provides C functions to compute ray-shape intersections for three representations of planetary body geometry: tri-axial ellipsoid, gridded Digital Elevation Model (DEM), and infinite plane. These are essential for photoclinometry, ortho-rectification, and geometric backplane generation.

## Supported Shape Models

| Model | Best For | Resolution | Computational Cost |
|---|---|---|---|
| Ellipsoid | Large-scale mapping, low-orbit imagery | Global | ~10 µs/ray |
| DEM | High-resolution ortho-rectification, local slope | Local grid | ~100 µs/ray (with interpolation) |
| Plane | Test/debug, ring systems | Infinite flat surface | ~1 µs/ray |

## API

### Shape Model Interface

```c
typedef enum {
    SHAPE_ELLIPSOID,
    SHAPE_DEM,
    SHAPE_PLANE
} ShapeModelType;

typedef struct {
    double x, y, z;  /* Cartesian intersection point */
    double lat, lon; /* Planetocentric latitude/longitude */
    double normal_x, normal_y, normal_z;  /* Surface normal */
    double dem_height;
} ShapeIntersection;

typedef struct PPdsShape PPdsShape;

PPdsShape *p_shapemodel_ellipsoid(double a, double b, double c);
PPdsShape *p_shapemodel_dem(const char *dem_filename, 
                            double a_ref, double b_ref, double c_ref);
PPdsShape *p_shapemodel_plane(void);

int p_shapemodel_ray_intersect(PPdsShape *shape,
                               const double ray_origin[3],
                               const double ray_direction[3],
                               ShapeIntersection *result);

int p_shapemodel_lat_lon_to_xyz(PPdsShape *shape,
                                double lat, double lon,
                                double *x, double *y, double *z);

void p_shapemodel_free(PPdsShape *shape);
```

## Shape Model Details

### Ellipsoid

**Equation:**
$$\frac{x^2}{a^2} + \frac{y^2}{b^2} + \frac{z^2}{c^2} = 1$$

where $a$, $b$, $c$ are semi-axes (equatorial, equatorial, polar).

**Ray Intersection:**

Substitute ray parameterization into ellipsoid equation:
$$\mathbf{r}(t) = \mathbf{o} + t \mathbf{d}$$

Yields quadratic in $t$:
$$At^2 + Bt + C = 0$$

Coefficients depend on ray origin $\mathbf{o}$ and direction $\mathbf{d}$.

**Surface Normal:**

$$\hat{n} = \nabla f = \left( \frac{2x}{a^2}, \frac{2y}{b^2}, \frac{2z}{c^2} \right) \text{ (unnormalized)}$$

### DEM

**Model:** Gridded elevation as bilinear interpolant over regular lat/lon grid.

**Ray Intersection:** Newton-Raphson iteration on altitude difference function:
$$f(\phi, \lambda) = h(\phi, \lambda) - \text{ray altitude at } (\phi, \lambda)$$

Converges in 3–5 iterations for near-nadir rays; slower for grazing incidence.

**Surface Normal:** Computed from 3×3 DEM kernel via central differences:

$$\frac{\partial h}{\partial \phi} \approx \frac{h_{i+1,j} - h_{i-1,j}}{2 \Delta\phi}$$

**Callback Mode:**

For external gridded data (HDF5, NetCDF), register user function:

```c
typedef int (*DEM_CALLBACK)(double lat, double lon, double *height);

p_shapemodel_dem_set_callback(PPdsShape *shape, DEM_CALLBACK func);
```

### Plane

Infinite plane at $z = 0$. Ray intersection:

$$t = -\frac{o_z}{d_z}$$

Only valid for upward-directed rays ($d_z > 0$). Used for ring systems and test cases.

## Compilation

### Standalone Compilation

```bash
gcc -DP_SHAPEMODEL_STANDALONE -fopenmp -I. \
    -c p_shapemodel.c -o p_shapemodel.o
gcc -o test_shapemodel test_shapemodel.c p_shapemodel.o -lm
```

### With DEM Support (GeoTIFF or Envi)

```bash
gcc -DP_SHAPEMODEL_STANDALONE -DUSE_GDAL -fopenmp \
    -I$(GDAL_INCLUDE) -c p_shapemodel.c -o p_shapemodel.o
gcc -o test_dem test_dem.c p_shapemodel.o -lgdal -lm
```

## Usage Example

Compute intercept for Mars HIRISE image using Martian ellipsoid:

```c
/* Mars IAU2000 ellipsoid: a=b=3396.19 km, c=3376.20 km */
PPdsShape *mars = p_shapemodel_ellipsoid(3396.19, 3396.19, 3376.20);

/* Ray from spacecraft toward surface */
double origin[3] = {0, 0, 400};  /* 400 km altitude above pole */
double direction[3] = {0, 0, -1}; /* downward */

ShapeIntersection hit;
if (p_shapemodel_ray_intersect(mars, origin, direction, &hit)) {
    printf("Lat: %.2f°, Lon: %.2f°\n", hit.lat, hit.lon);
}

p_shapemodel_free(mars);
```

## Scientific References

- Hapke, B. W. (1993). *Theory of Reflectance and Emittance Spectroscopy*. Cambridge University Press. ISBN 0-521-30789-9.
- Seidelmann, P. K., Archinal, B. A., A'Hearn, M. F., et al. (2007). "Report of the IAU/IAG Working Group on Cartographic Coordinates and Rotational Elements: 2006." *Celestial Mechanics and Dynamical Astronomy*, 98(3), 155–180. https://doi.org/10.1007/s10569-007-9072-y
- Archinal, B. A., A'Hearn, M. F., Bowell, E., et al. (2018). "Report of the IAU Working Group on Cartographic Coordinates and Rotational Elements: 2015." *Celestial Mechanics and Dynamical Astronomy*, 130(3), 22. https://doi.org/10.1007/s10569-017-9805-5

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
