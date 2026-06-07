# p.horizon.gpu testsuite

Two test files, designed to run on any dev PC with or without a GPU:

| File | Purpose | OpenCL? |
|------|---------|---------|
| `test_p_horizon_gpu.py` | GRASS-integration tests against the compiled `p.horizon.gpu` binary. Uses synthetic DEMs in an XY temp location, asserts horizon values against analytical answers, and checks the `-c` (OpenMP) flag still matches the default backend. | Auto-detected: GPU ICD → GPU; CPU-only with PoCL → CPU-OpenCL; nothing → OpenMP fallback. Tests still pass either way. |
| `test_horizon_kernel_proto.py` | Pure-Python pyopencl prototype self-test (Gaussian-hill DEM, parity against a CPU reference). | **Required** (skips cleanly if `pyopencl` or any OpenCL ICD is missing). |

## Running

From the repo root:
```sh
grass --tmp-location XY --exec python -m pytest \
    p.horizon.gpu/testsuite/ -v
```

The first thing the GRASS-integration test prints is a one-liner banner showing
which backend was detected, e.g.:
```
[p.horizon.gpu testsuite] backend=opencl  (OpenCL device)
[p.horizon.gpu testsuite] backend=openmp  (OpenMP fallback)
```

## Full backend coverage on a CPU-only dev PC

To exercise the OpenCL code path without a GPU, install PoCL (CPU-side
OpenCL runtime, available via apt on Debian/Ubuntu):
```sh
sudo apt install pocl-opencl-icd
```
PoCL registers itself at `/etc/OpenCL/vendors/pocl.icd`; `p.horizon.gpu`'s
device picker prefers `CL_DEVICE_TYPE_GPU` first and falls through to
`CL_DEVICE_TYPE_ALL`, so PoCL's CPU device is selected automatically.

## What's NOT tested here

* **Conformality guard** (rejection of PROJECTION_LL and non-conformal
  projected CRS) — needs a real projected location to set up; covered
  manually via the smoke runs.
* **Real-DEM agreement with r.horizon** — the two modules are
  numerically distinct on polar projections by design (r.horizon uses a
  large-step direction estimator; this module uses a local-tangent
  rotation plane). See `p.horizon.gpu.html` for the rationale.

## Prerequisites

* GRASS 8.6+ with the `p.horizon.gpu` binary on the module path (i.e. the
  `p-landing-grass` deb installed, or `make -f Makefile.standalone` in
  `p.horizon.gpu/` and `GRASS_ADDON_PATH` set to include it).
* For the proto test: `python3-numpy` and `python3-pyopencl` (the latter
  is what's missing on bare Debian trixie; install via
  `sudo apt install python3-pyopencl` if available, else just let it
  skip).
