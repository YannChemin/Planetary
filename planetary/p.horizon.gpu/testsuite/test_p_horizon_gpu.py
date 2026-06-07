"""
Testsuite for p.horizon.gpu (OpenCL+OpenMP horizon caster).

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.horizon.gpu/testsuite/test_p_horizon_gpu.py -v

Cross-PC story:
  * With a GPU + vendor OpenCL ICD       → tests exercise the GPU path.
  * With pocl-opencl-icd installed       → tests exercise the CPU-OpenCL
                                            path (PoCL is CPU-side OpenCL,
                                            available via apt on Debian).
  * With no OpenCL ICD at all            → p.horizon.gpu falls back to
                                            OpenMP transparently; tests
                                            still pass, just exercising
                                            the OpenMP backend twice.
  The active backend is reported once at module import (see banner below);
  no test fails for lack of OpenCL — install pocl-opencl-icd on bare CPUs
  if you want full backend coverage.

Covers:
  * Flat DEM → horizon ≈ 0 at every azimuth.
  * Single tall pillar → horizon at the azimuth aimed at the pillar
    matches atan(h/d) within tolerance.
  * Output filename convention matches r.horizon (basename_NNN_F).
  * The -c flag forces the OpenMP backend and still produces the same
    horizons (within float tolerance) as the default backend.

Conformality guard tests (PROJECTION_LL rejected, non-conformal CRS
rejected) are not exercised here because they require a real projected
location; the synthetic DEMs run in an XY temp location where the guard
is bypassed (no rotation plane needed) and the kernel still runs cleanly.
"""

import math
import os
import subprocess
import sys
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

HAS_HORGPU = bool(gs.find_program("p.horizon.gpu", "--help"))


def _detect_backend():
    """Return ('opencl'|'openmp'|'unknown', device_name_or_msg).

    Runs a tiny one-cell horizon job in a temp region and parses the
    'Backend: …' line p.horizon.gpu emits to stderr. Result is printed
    once at module import so the CI/dev sees which path got exercised.
    """
    if not HAS_HORGPU:
        return ("unavailable", "p.horizon.gpu not on module path")
    try:
        gs.use_temp_region()
        gs.run_command("g.region", n=100, s=0, e=100, w=0, res=10, quiet=True)
        gs.mapcalc("_thg_probe = 1000.0", overwrite=True, quiet=True)
        env = os.environ.copy()
        env["GRASS_VERBOSE"] = "2"
        r = subprocess.run(
            ["p.horizon.gpu", "elevation=_thg_probe", "output=_thg_probe_h",
             "direction=0", "maxdistance=50", "--overwrite"],
            capture_output=True, text=True, env=env, timeout=30)
        gs.run_command("g.remove", type="raster",
                       name="_thg_probe,_thg_probe_h_000_0",
                       flags="f", quiet=True)
        gs.del_temp_region()
        log = (r.stderr or "") + (r.stdout or "")
        if "Backend: OpenCL" in log:
            return ("opencl", log.split("Backend: OpenCL", 1)[0]
                              .splitlines()[-1] if False else "OpenCL device")
        if "Backend: OpenMP" in log:
            return ("openmp", "OpenMP fallback")
        return ("unknown", "no Backend: line in output")
    except Exception as e:
        return ("error", str(e))


_BACKEND, _BACKEND_MSG = _detect_backend()
print(f"\n[p.horizon.gpu testsuite] backend={_BACKEND}  ({_BACKEND_MSG})\n",
      file=sys.stderr)


def _read(name):
    """Return a (min, max, mean) tuple for raster `name`, floats."""
    info = gs.parse_command("r.univar", map=name, flags="g")
    return float(info["min"]), float(info["max"]), float(info["mean"])


class TestHorizonGpuFlat(TestCase):
    """Flat DEM ⇒ horizon angle is 0 at every azimuth."""

    dem = "thg_flat_dem"
    base = "thg_flat_hor"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        # 200 × 200 @ 10 m  → 2 × 2 km square, plenty of room for rays.
        gs.run_command("g.region", n=2000, s=0, e=2000, w=0, res=10)
        gs.mapcalc(f"{cls.dem} = 1000.0", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", pattern=f"{cls.base}_*",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="raster", name=cls.dem,
                       flags="f", quiet=True)
        cls.del_temp_region()

    @test.skipIf(not HAS_HORGPU, "compiled p.horizon.gpu not on module path")
    def test_flat_dem_zero_horizon(self):
        self.assertModule("p.horizon.gpu",
                          elevation=self.dem, output=self.base,
                          start=0, end=360, step=90,
                          maxdistance=500, bodyradius=1.0e12,
                          overwrite=True)
        for suf in ("000_0", "090_0", "180_0", "270_0"):
            name = f"{self.base}_{suf}"
            self.assertRasterExists(name)
            mn, mx, _ = _read(name)
            self.assertLess(abs(mn), 0.01,
                            msg=f"{name} min={mn} not ≈ 0")
            self.assertLess(abs(mx), 0.01,
                            msg=f"{name} max={mx} not ≈ 0")


class TestHorizonGpuPillar(TestCase):
    """
    Single tall pillar at known offset: the ray walking toward it must
    see a horizon angle ≈ atan(h/d); rays walking away see 0.
    """

    dem = "thg_pillar_dem"
    base = "thg_pillar_hor"

    # Pillar geometry (chosen so the analytical horizon is a clean number).
    cell = 10.0       # m
    h = 500.0         # pillar height (m above plain at 1000 m)
    # Pillar centred 50 cells east of the row centre → d = 500 m.
    # atan(500/500) = 45° exactly.

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1000, s=0, e=1000, w=0,
                       res=cls.cell)
        # Pillar at (col=75, row=50) ≈ projected (x=755, y=505) in 100×100 cells.
        # We will probe at (col=25, row=50) — 50 cells west = 500 m.
        # mapcalc indexes col() from 1; place pillar at col()==75, row()==50.
        gs.mapcalc(
            f"{cls.dem} = if(col()==75 && row()==50, 1500.0, 1000.0)",
            overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", pattern=f"{cls.base}_*",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="raster", name=cls.dem,
                       flags="f", quiet=True)
        cls.del_temp_region()

    @test.skipIf(not HAS_HORGPU, "compiled p.horizon.gpu not on module path")
    def test_pillar_horizon_eastward(self):
        # bodyradius huge → curvature term ≈ 0 → analytical check is clean.
        self.assertModule("p.horizon.gpu",
                          elevation=self.dem, output=self.base,
                          direction=0,   # azimuth 0 = east (CCW from east)
                          maxdistance=900, bodyradius=1.0e12,
                          overwrite=True)
        name = f"{self.base}_000_0"
        self.assertRasterExists(name)
        # Probe at (col=25, row=50): query a single-cell region.
        gs.run_command("g.region", n=505, s=495, e=255, w=245, res=self.cell)
        try:
            mn, mx, mean = _read(name)
            # Single-cell probe → min=max=mean. Should be ≈ atan(500/500)=45°.
            self.assertAlmostEqual(mean, 45.0, delta=1.5,
                                   msg=f"east horizon {mean}° not ≈ 45°")
        finally:
            # Restore full test region for tearDown raster removal.
            gs.run_command("g.region", n=1000, s=0, e=1000, w=0,
                           res=self.cell)


class TestHorizonGpuCpuFlag(TestCase):
    """The -c flag forces OpenMP; outputs must match the default backend
    on a flat DEM (trivially 0 either way) and on the pillar DEM (within
    float tolerance)."""

    dem = "thg_cpu_dem"
    base_def = "thg_cpu_def"
    base_omp = "thg_cpu_omp"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1000, s=0, e=1000, w=0, res=10)
        gs.mapcalc(
            f"{cls.dem} = if(col()==75 && row()==50, 1500.0, 1000.0)",
            overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster",
                       pattern=f"{cls.base_def}_*,{cls.base_omp}_*",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="raster", name=cls.dem,
                       flags="f", quiet=True)
        cls.del_temp_region()

    @test.skipIf(not HAS_HORGPU, "compiled p.horizon.gpu not on module path")
    def test_cpu_flag_matches_default(self):
        common = dict(elevation=self.dem, direction=0, maxdistance=900,
                      bodyradius=1.0e12, overwrite=True)
        self.assertModule("p.horizon.gpu", output=self.base_def, **common)
        self.assertModule("p.horizon.gpu", output=self.base_omp, flags="c",
                          **common)
        diff = "thg_cpu_diff"
        try:
            gs.mapcalc(
                f"{diff} = abs({self.base_def}_000_0 - {self.base_omp}_000_0)",
                overwrite=True)
            _, mx, _ = _read(diff)
            # Same algorithm both sides → expect machine-noise differences.
            self.assertLess(mx, 0.1,
                            msg=f"OpenMP vs default differ by {mx}°")
        finally:
            gs.run_command("g.remove", type="raster", name=diff,
                           flags="f", quiet=True)


class TestHorizonGpuFilenameConvention(TestCase):
    """Output rasters use r.horizon's NNN_F suffix so p_lib's interpolator
    finds them unchanged."""

    dem = "thg_name_dem"
    base = "thg_name_hor"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=500, s=0, e=500, w=0, res=10)
        gs.mapcalc(f"{cls.dem} = 1000.0", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", pattern=f"{cls.base}_*",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="raster", name=cls.dem,
                       flags="f", quiet=True)
        cls.del_temp_region()

    @test.skipIf(not HAS_HORGPU, "compiled p.horizon.gpu not on module path")
    def test_suffix_matches_r_horizon(self):
        self.assertModule("p.horizon.gpu",
                          elevation=self.dem, output=self.base,
                          start=0, end=45, step=22.5,
                          maxdistance=200, overwrite=True)
        # Expect 022_5 and 000_0 — r.horizon's "%03d_%d" naming.
        for suf in ("000_0", "022_5"):
            self.assertRasterExists(f"{self.base}_{suf}")


if __name__ == "__main__":
    test()
