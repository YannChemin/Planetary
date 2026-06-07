"""
Pytest wrapper around the standalone prototype self-test.

This stays in testsuite/ so `pytest` picks it up. It defers the heavy
work to proto/horizon_pyopencl.py so the kernel logic has one source of
truth. Skips cleanly when pyopencl or any OpenCL ICD is missing.

Phase 2 (GRASS wrapper) will add testsuite/test_p_horizon_gpu.py that
calls the built module via gs.run_command and checks parity against
r.horizon on a real DEM clip.
"""

import os
import sys
import math
import pytest

_PROTO = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      os.pardir, "proto")
sys.path.insert(0, _PROTO)


def _have_opencl():
    try:
        import pyopencl  # noqa
        platforms = pyopencl.get_platforms()
        return bool(platforms)
    except Exception:
        return False


@pytest.mark.skipif(not _have_opencl(),
                    reason="pyopencl / OpenCL ICD not available")
def test_kernel_matches_cpu_reference_on_gaussian_hill():
    from horizon_pyopencl import (HorizonGPU, _gaussian_hill_dem,
                                  _analytic_horizon_for_observer_at_origin)
    import numpy as np

    dem, cell = _gaussian_hill_dem(n=51, cell_m=10.0)
    hgpu = HorizonGPU(dem, cell_m=cell, body_radius_m=1e12)

    step = 0.5 * cell
    max_d = (max(dem.shape) - 1) * cell
    azs = [0.0, 45.0, 90.0, 180.0, 270.0]
    gpu = hgpu.run(azs, step_m=step, max_dist_m=max_d)

    for az in azs:
        cpu = _analytic_horizon_for_observer_at_origin(
            dem, cell, az, max_dist_m=max_d, step_m=step)
        mask = ~(np.isnan(gpu[az]) | np.isnan(cpu))
        if not mask.any():
            continue
        worst = float(np.abs(gpu[az] - cpu)[mask].max())
        # 0.01 rad ≈ 0.57°; bilinear-vs-integer at first ray step toward
        # grid edges contributes ~0.4° at az=180/270.
        assert worst < 0.01, (
            f"az={az}°: worst |gpu-cpu| = {math.degrees(worst):.4f}°")


@pytest.mark.skipif(not _have_opencl(),
                    reason="pyopencl / OpenCL ICD not available")
def test_deterministic_across_runs():
    """Two consecutive runs on the same input must produce identical
    output (a regression against any accidental non-determinism in the
    kernel)."""
    from horizon_pyopencl import HorizonGPU, _gaussian_hill_dem
    import numpy as np

    dem, cell = _gaussian_hill_dem(n=33, cell_m=10.0)
    hgpu = HorizonGPU(dem, cell_m=cell, body_radius_m=1e12)
    a = hgpu.run([0.0, 90.0, 180.0], step_m=cell * 0.5,
                 max_dist_m=20 * cell)
    b = hgpu.run([0.0, 90.0, 180.0], step_m=cell * 0.5,
                 max_dist_m=20 * cell)
    for az in (0.0, 90.0, 180.0):
        # NaNs compare unequal under ==; allow equal nan pattern + equal
        # finite values.
        nan_a = np.isnan(a[az])
        nan_b = np.isnan(b[az])
        assert np.array_equal(nan_a, nan_b)
        m = ~nan_a
        assert np.array_equal(a[az][m], b[az][m])
