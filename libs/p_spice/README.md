# p_spice Library

NAIF CSPICE geometry toolkit wrapper for planetary image geolocation and geometric backplane generation.

## Overview

The p_spice library wraps the NAIF Cassini-Huygens SPICE library (N0067 version) to provide C-friendly functions for computing geometric relationships between planetary bodies, spacecraft, and instruments. SPICE kernels encode spacecraft trajectories (SPK), instrument boresights (IK), camera orientation (CK), and planetary reference frames (PCK, FK).

## Key Concepts

### Kernel Types

| Kernel | Content | Example File |
|---|---|---|
| LSK | Leap second data | `naif0012.tls` |
| PCK | Planetary ephemeris & orientation | `pck00010.tpc` |
| SPK | Spacecraft/body position | `sat393s.bsp` |
| CK | Instrument/camera orientation | `08311_08001rv.bc` |
| IK | Instrument optical properties | `cassini_iss.ti` |
| FK | Reference frame definitions | `cassini_frames.tf` |
| SCLK | Spacecraft clock correlation | `cas00178.tsc` |

### Meta-Kernel

A single `.mk` file lists all kernels for a mission:

```
KPL/MK

KERNELS_TO_LOAD = (
  'kernels/lsk/naif0012.tls'
  'kernels/pck/pck00010.tpc'
  'kernels/spk/sat393s.bsp'
  'kernels/ck/08311_08001rv.bc'
  'kernels/ik/cassini_iss.ti'
)
```

## API

### Core Functions

```c
typedef struct {
    double x, y, z;        /* Cartesian coordinates, km */
    double lat, lon;       /* Latitude, longitude, radians */
    double incidence;      /* Solar incidence, degrees */
    double emission;       /* Spacecraft emission, degrees */
    double phase;          /* Phase angle, degrees */
    double pixel_scale;    /* m/pixel at surface */
} PPdsGeometry;

/* Initialize SPICE with kernel path */
int p_spice_init(const char *meta_kernel_path);

/* Cleanup SPICE resources */
void p_spice_cleanup(void);

/* Compute geometry at a single point */
int p_spice_sincpt(const char *body,
                   const char *obsrvtry,
                   const char *instrument,
                   double sclk_time,
                   int img_row, int img_col,
                   PPdsGeometry *geom);

/* Compute illumination angles */
int p_spice_ilumin(const char *body,
                   const char *obsrvtry,
                   double et_time,
                   double lat, double lon,
                   double *incidence, double *emission,
                   double *phase);

/* Get target body information */
int p_spice_target_info(const char *body,
                        double *radius_a, double *radius_b, 
                        double *radius_c);
```

### Geometry Outputs

**p_spice_sincpt** (Surface Intercept):
- Backproject image pixel to surface
- Compute incidence/emission/phase angles
- Return planetocentric lat/lon
- Pixel scale in m/pixel

**p_spice_ilumin** (Illumination):
- Compute solar incidence angle
- Compute spacecraft emission angle
- Compute phase angle (solar-target-spacecraft)
- Sun angle range: 0° (subsolar) to 180° (antisolar)

## Compilation

### Standalone Compilation

Requires NAIF CSPICE library installation (e.g., `/home/yann/dev/cspice`):

```bash
# Build p_spice.o
gcc -DP_SPICE_STANDALONE -fopenmp -I. \
    -I/home/yann/dev/cspice/include \
    -c p_spice.c -o p_spice.o

# Link test executable
gcc -o test_spice test_spice.c p_spice.o \
    /home/yann/dev/cspice/lib/cspice.a -lm
```

### Integration with GRASS

Compiled as part of p.phocube, p.spiceinit, p.cam2map, p.caminfo, p.target.info modules. CSPICE location auto-detected via `pkg-config cspice`.

## Threading Model

**IMPORTANT:** NAIF CSPICE is **not thread-safe** due to internal state in kernel-loading routines.

**Safe parallelization strategy:**

```c
#pragma omp parallel for collapse(2) schedule(static)
for (int row = 0; row < nrows; row++) {
    for (int col = 0; col < ncols; col++) {
        PPdsGeometry geom;
        /* SPICE calls serialized within critical section */
        #pragma omp critical(spice_geom)
        {
            p_spice_sincpt("Moon", "LRO", "LROC_NAC",
                          sclk_time, row, col, &geom);
        }
        process_geometry(row, col, geom);
    }
}
```

Overhead is ~1 µs per pixel for critical section; acceptable for 0.1–1 Mpixel images.

## Usage Example

Compute geometry for Cassini ISS image of Saturn:

```c
/* Load SPICE kernels via meta-kernel */
if (p_spice_init("/path/to/cassini_meta.mk") != 0) {
    fprintf(stderr, "Failed to initialize SPICE\n");
    return 1;
}

/* Image metadata */
double sclk_time = 1234567890.0;  /* Cassini spacecraft clock */
int img_row = 512, img_col = 512;

/* Compute geometry */
PPdsGeometry geom;
if (p_spice_sincpt("SATURN", "CASSINI", "CASSINI_ISS_WAC",
                   sclk_time, img_row, img_col, &geom)) {
    fprintf(stderr, "SPICE error\n");
} else {
    printf("Lat: %.2f°, Lon: %.2f°\n", geom.lat * 180/M_PI, 
           geom.lon * 180/M_PI);
    printf("Incidence: %.2f°, Emission: %.2f°, Phase: %.2f°\n",
           geom.incidence, geom.emission, geom.phase);
}

p_spice_cleanup();
return 0;
```

## Scientific References

- Acton, C. H. (1996). "Ancillary Data Services of NASA's Navigation and Ancillary Information Facility." *Planetary and Space Science*, 44(1), 65–70. https://doi.org/10.1016/0032-0633(95)00107-7
- Acton, C., Bachman, N., Semenov, B., & Wright, E. (2018). "A Look toward the Future in the Handling of Space Science Mission Geometry." *Planetary and Space Science*, 150, 9–12. https://doi.org/10.1016/j.pss.2017.02.013
- NAIF SPICE Toolkit. "Cassini-Huygens SPICE Archive." https://naif.jpl.nasa.gov/naif/data_archived/cassini/ (current version: N0067)
- JPL Navigation and Ancillary Information Facility (NAIF). "Introduction to SPICE." https://naif.jpl.nasa.gov/naif/basics.html

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.

## Note on SPICE Kernel Licensing

NAIF SPICE kernels are generally in the public domain or under NASA's Standard Clauses and Notices. Always check individual kernel headers and NASA Planetary Data System policies when redistributing.
