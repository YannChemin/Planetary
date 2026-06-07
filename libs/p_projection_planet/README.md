# p_projection_planet Library

Planetary coordinate projections specialized for ring systems and high-latitude mapping.

## Overview

The p_projection_planet library extends standard map projections (Snyder 1987) with three specialized projections for planetary ring systems and ellipsoid-based bodies, providing both forward (lat/lon → x/y) and inverse (x/y → lat/lon) transformations.

## Supported Projections

| Projection | Use Case | Forward | Inverse | Special Features |
|---|---|---|---|---|
| Ring Cylindrical | Ring radial profiles | ✓ | ✓ | Radius (km) × angle (°) |
| Lunar Azimuthal Equal-Area | Polar mapping | ✓ | ✓ | Preserves area, north polar origin |
| Upturned Ellipsoid TA | Ring systems with ellipsoid bulge | ✓ | ✓ | Newton-Raphson inverse |

## API

### Projection Transform Functions

```c
typedef enum {
    PROJ_RING_CYLINDRICAL,
    PROJ_LUNAR_AZIMUTHAL_EA,
    PROJ_UPTURNED_TA
} ProjectionType;

typedef struct PPdsProjection PPdsProjection;

PPdsProjection *p_projection_create(ProjectionType type,
                                    double center_lat, double center_lon,
                                    double radius_ref);

int p_projection_forward(PPdsProjection *proj,
                         double lat, double lon,
                         double *x, double *y);

int p_projection_inverse(PPdsProjection *proj,
                         double x, double y,
                         double *lat, double *lon);

void p_projection_free(PPdsProjection *proj);
```

## Projection Details

### Ring Cylindrical

Direct mapping of planetocentric coordinates to cylindrical (ring) coordinates.

**Forward:**
$$x = r \cos(\lambda)$$
$$y = r \sin(\lambda)$$

where:
- $r$ = Saturn ring radius, km (from PCK or input)
- $\lambda$ = planetocentric longitude, radians
- Origin: ring centre

**Inverse:**
$$r = \sqrt{x^2 + y^2}$$
$$\lambda = \text{atan2}(y, x)$$

**Use:** Radial brightness profiles (p.rings.stats), annular statistics.

### Lunar Azimuthal Equal-Area

Centered at north pole; equal-area projection preserves brightness counts.

**Forward (North Pole):**
$$\rho = 2R \sin\left(\frac{\pi}{4} - \frac{\phi}{2}\right)$$
$$x = \rho \cos(\lambda)$$
$$y = \rho \sin(\lambda)$$

where:
- $R$ = planetary radius
- $\phi$ = latitude
- $\lambda$ = longitude

**Inverse (North Pole):**
$$\rho = \sqrt{x^2 + y^2}$$
$$\phi = \frac{\pi}{2} - 2 \arcsin\left(\frac{\rho}{2R}\right)$$
$$\lambda = \text{atan2}(y, x)$$

**Note:** South pole variant computed by latitude inversion.

### Upturned Ellipsoid Transverse Azimuthal

Specialized for ring systems with tri-axial ellipsoid reference; Upturned geometry accommodates ring plane above ellipsoid.

**Forward:**
$$x = R_{\text{ring}} (\lambda - \lambda_0)$$
$$y = f(r, \phi)$$

where $f(\cdot)$ includes ellipsoid-dependent scaling.

**Inverse (Newton-Raphson):**

Solve iteratively for $(\lambda, r)$ given $(x, y)$ using Jacobian:

$$\mathbf{J} = \begin{bmatrix} \frac{\partial x}{\partial \lambda} & \frac{\partial x}{\partial r} \\ \frac{\partial y}{\partial \lambda} & \frac{\partial y}{\partial r} \end{bmatrix}$$

Convergence: 4–6 iterations for accuracies better than 0.1 km.

## Compilation

### Standalone Compilation

```bash
gcc -DP_PROJECTION_PLANET_STANDALONE -fopenmp -I. \
    -c p_projection_planet.c -o p_projection_planet.o
gcc -o test_projection test_projection.c p_projection_planet.o -lm
```

### Integration with GRASS

Compiled as part of p.cam2map and p.rings.project modules.

## Usage Example

Map Saturn ring radius 100,000 km at longitude 45° using Ring Cylindrical:

```c
PPdsProjection *proj = p_projection_create(PROJ_RING_CYLINDRICAL,
                                           0.0, 0.0,
                                           120000.0); /* reference radius */

double lat = 0.0, lon = 45.0;
double x, y;
if (p_projection_forward(proj, lat, lon, &x, &y)) {
    printf("x=%.2f km, y=%.2f km\n", x, y);
}

p_projection_free(proj);
```

## Scientific References

- Snyder, J. P. (1987). "Map Projections — A Working Manual." U.S. Geological Survey Professional Paper 1395. https://doi.org/10.3133/pp1395
- French, R. G., Nicholson, P. D., Coker, R. L., et al. (1993). "Geometry of the Saturn System from the 1989 Occultation of 28 Sgr by the Rings." *Icarus*, 103(2), 163–214. https://doi.org/10.1006/icar.1993.1066
- Porco, C. C., Baker, E., Barbara, J., et al. (2005). "Cassini Imaging Science: Instrument Characteristics and Anticipated Scientific Investigations at Saturn." *Science*, 307(5713), 1226–1236. https://doi.org/10.1126/science.1108056
- Archinal, B. A., A'Hearn, M. F., Bowell, E., et al. (2018). "Report of the IAU Working Group on Cartographic Coordinates and Rotational Elements: 2015." *Celestial Mechanics and Dynamical Astronomy*, 130(3), 22. https://doi.org/10.1007/s10569-017-9805-5

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
