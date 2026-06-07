## DESCRIPTION

**p.sunmask** computes a terrain shadow mask from a DEM and an explicit sun
position (altitude and azimuth), using parallel processing on all available
CPU cores (OpenMP) and optionally on a GPU or CPU accelerator via OpenCL.

It is a drop-in replacement for *r.sunmask* (altitude/azimuth mode) with the
same output convention: **1 = sunlit, 0 = shadow, NULL = nodata**.

### Algorithm

For each output pixel P at position (r, c) with elevation h₀:

1. Cast a ray from P *toward the sun* in the direction given by `azimuth`
   (from North, clockwise) at steps of 0.5 pixels.
2. At each sample point Q along the ray, bilinearly interpolate the DEM to
   get elevation hQ.
3. Compute the ray height at horizontal distance d metres:
   h_ray = h₀ + d × tan(altitude).
4. If hQ > h_ray: P is in shadow (output 0).
5. If the ray exits the raster without occlusion: P is sunlit (output 1).

All pixels are independent, making the algorithm embarrassingly parallel.

### Parallelism

**OpenMP (always):** The outer pixel loop is parallelised with
`#pragma omp parallel for` across all available CPU threads. Set
`OMP_NUM_THREADS` to control the thread count.

**OpenCL (optional, probed at runtime):** If the module was built with
`HAVE_OPENCL` and an OpenCL platform is found at runtime, the shadow kernel
runs on the first available GPU device, or on the CPU device if no GPU is
present (e.g. via PoCL). Falls back silently to OpenMP if no platform is found.

Use the **-c** flag to force CPU/OpenMP even when OpenCL is available.

### Memory

The entire DEM is loaded into RAM as a float array. Memory requirement:
`nrows × ncols × 4 bytes`. For the LOLA South Polar 5 m DEM
(30336×30336) ≈ 3.5 GB; for a 10×10 km region at 5 m (2000×2000) = 16 MB.

## PARAMETERS

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `elevation` | raster | *required* | Input DEM raster (metres) |
| `output` | raster | *required* | Output shadow mask (0=shadow, 1=sunlit) |
| `altitude` | float | 0–89.999 | Sun elevation angle above horizon (degrees) |
| `azimuth` | float | 0–360 | Sun azimuth angle from North, clockwise (degrees) |

## FLAGS

| Flag | Description |
|------|-------------|
| `-c` | Force CPU/OpenMP path even if OpenCL is available |

## INSTALLATION

**Dependencies:**

- OpenMP: provided by GCC ≥ 4.2; no extra package needed.
- OpenCL headers: `sudo apt install opencl-c-headers`
- OpenCL ICD loader: `sudo apt install ocl-icd-opencl-dev`
- OpenCL runtime (choose one or more):
  - CPU only: `sudo apt install pocl-opencl-icd`
  - Intel GPU/CPU: Intel oneAPI OpenCL runtime
  - NVIDIA GPU: CUDA toolkit (includes OpenCL ICD)
  - AMD GPU: ROCm OpenCL

**Build and install:**

```bash
cd p.sunmask
make MODULE_TOPDIR=$(grass --config path)
sudo make install MODULE_TOPDIR=$(grass --config path)
```

The Makefile auto-detects OpenCL at build time via `pkg-config`. If OpenCL
is not found the module is built with OpenMP only.

## EXAMPLES

```bash
# Basic usage (identical to r.sunmask altitude/azimuth mode)
p.sunmask elevation=dem output=shadow altitude=5.0 azimuth=180.0

# Force CPU/OpenMP path
p.sunmask -c elevation=dem output=shadow altitude=5.0 azimuth=180.0

# Control thread count
OMP_NUM_THREADS=8 p.sunmask elevation=dem output=shadow altitude=5.0 azimuth=90.0
```

## NOTES

p.sunmask uses the same flat-earth ray-march approximation as *r.sunmask*.
This is accurate for study areas smaller than a few hundred kilometres.

The output is integer CELL type: values 0 and 1. To match *r.sunmask*'s
convention (1=sunlit, NULL=shadow), apply: `r.null map=output null=0`

*p.illumination.sunfraction* applies this internally, so no extra step is
needed in the pipeline.

## SEE ALSO

*[r.sunmask](https://grass.osgeo.org/grass-stable/manuals/r.sunmask.html),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.illumination.shadow](p.illumination.shadow.md)*

## REFERENCES

- OpenMP specification: <https://www.openmp.org/>
- OpenCL specification: <https://www.khronos.org/opencl/>
- PoCL (CPU OpenCL runtime): <http://portablecl.org/>

## AUTHOR

Yann Chemin
