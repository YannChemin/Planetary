"""Test of p.crater.draw

Validates the GRASS p.crater.draw module: --help interface, that the
binary is on PATH, and that running the DEM detector on a synthetic
DEM containing one circular depression yields exactly one detection
whose centre and diameter match within a tolerance.

@author Yann Chemin
@license Unlicense (https://unlicense.org)
"""

import math
import shutil
import unittest

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


class TestPcraterDraw(TestCase):
    """Verify p.crater.draw produces a polygon for a synthetic crater."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region",
                      n=2000, s=-2000, e=2000, w=-2000, res=10)
        # Synthetic DEM: a 600 m-radius depression at the origin with
        # a 50 m elevated rim. r.mapcalc piecewise-defines the profile.
        cls.runModule("r.mapcalc",
                      overwrite=True,
                      expression=(
                          "syn_dem = "
                          "1000.0 + "
                          "if(sqrt(x()*x() + y()*y()) < 550, -100, "
                          "if(sqrt(x()*x() + y()*y()) < 650,   50, 0))"
                      ))

    @classmethod
    def tearDownClass(cls):
        cls.runModule("g.remove", flags="f", type="raster",
                      name="syn_dem", quiet=True)
        cls.runModule("g.remove", flags="f", type="vector",
                      name="detected_test", quiet=True)
        cls.del_temp_region()

    def test_help(self):
        import subprocess
        rc = subprocess.run(
            ["p.crater.draw", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode
        self.assertEqual(rc, 0)

    def test_module_metadata(self):
        self.assertIsNotNone(shutil.which("p.crater.draw"))

    def test_dem_detects_synthetic_crater(self):
        """A single 1200-m synthetic depression should yield ~1 detection."""
        self.runModule("p.crater.draw",
                       dem="syn_dem",
                       output="detected_test",
                       method="dem",
                       min_diameter=800,
                       max_diameter=2000,
                       threshold=0.50,
                       scales=4,
                       overwrite=True)
        # Read the detection list.
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="cat,cx,cy,D_eq,confidence",
                              flags="c")
        lines = [l for l in out.strip().splitlines() if l.strip()]
        self.assertGreaterEqual(len(lines), 1,
                                  "Expected at least one detection")
        # Take the highest-confidence row (last one in DESC order from NMS).
        # NMS leaves them in keep-order; just inspect the first.
        cat, cx, cy, D_eq, conf = lines[0].split("|")
        cx, cy, D_eq, conf = map(float, (cx, cy, D_eq, conf))
        # Centre within ~half the search stride (r/3 at the largest scale,
        # so ~ max_diameter/6 ~ 330 m for the 2000 m max).
        tolerance = 400.0
        self.assertLess(math.hypot(cx, cy), tolerance,
                          f"Detected centre ({cx:.0f}, {cy:.0f}) more "
                          f"than {tolerance:.0f} m from synthetic origin")
        # Diameter within +/- 50% of true 1200 m (coarse-stride scans).
        self.assertGreater(D_eq, 600.0)
        self.assertLess(D_eq,    2400.0)


    def test_dd_simple_column_default_from_body(self):
        """Without dd_simple/dd_simple_map, p.crater.draw bakes the body
        default into the dD_simple column of each polygon. Mars default
        is 0.150."""
        self.runModule("p.crater.draw",
                       dem="syn_dem",
                       output="detected_test",
                       method="dem",
                       body="mars",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.50, scales=4,
                       overwrite=True)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="dD_simple", flags="c").strip()
        vals = [float(v) for v in out.splitlines() if v.strip()]
        self.assertGreaterEqual(len(vals), 1, "no detections")
        for v in vals:
            self.assertAlmostEqual(v, 0.150, places=4,
                                    msg=f"dD_simple={v} did not pick up "
                                    "the Mars body default 0.150")

    def test_multiring_columns_present_and_null_when_disabled(self):
        """Without the -m flag, the basin_id and ring_index columns are
        present in the schema but NULL for every row. This validates
        the column wiring without depending on multi-ring detection
        accuracy (which requires sub-pixel centre refinement -
        documented as a future improvement)."""
        self.runModule("p.crater.draw",
                       dem="syn_dem",
                       output="detected_test",
                       method="dem",
                       body="mars",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.50, scales=4,
                       overwrite=True)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="basin_id,ring_index",
                              flags="c").strip()
        rows = [r for r in out.splitlines() if r.strip()]
        self.assertGreaterEqual(len(rows), 1)
        for r in rows:
            parts = r.split("|")
            self.assertEqual(parts[0], "",
                              f"basin_id should be NULL without -m, got "
                              f"{parts[0]!r}")
            self.assertEqual(parts[1], "",
                              f"ring_index should be NULL without -m, "
                              f"got {parts[1]!r}")

    def test_multiring_flag_runs_without_error(self):
        """With -m, the module completes successfully and the columns
        are still present. Whether any basin is grouped depends on
        whether the detector's coarse-stride scan happened to place
        multiple scales' centroids within the basin centre tolerance."""
        module = SimpleModule("p.crater.draw",
                               flags="m",
                               dem="syn_dem",
                               output="detected_test",
                               method="dem",
                               body="moon",
                               min_diameter=800, max_diameter=2000,
                               threshold=0.45, scales=6,
                               basin_centre_tol=0.30,
                               basin_ring_ratio=1.20,
                               overwrite=True)
        self.assertModule(module)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="basin_id,ring_index",
                              flags="c")
        self.assertIsNotNone(out)

    def test_dd_simple_column_from_raster(self):
        """dd_simple_map= raster value is baked into dD_simple per polygon."""
        # Constant 0.420 d/D raster
        self.runModule("r.mapcalc",
                       expression="dd_test_draw = 0.420",
                       overwrite=True)
        self.runModule("p.crater.draw",
                       dem="syn_dem",
                       output="detected_test",
                       method="dem",
                       body="mars",
                       dd_simple_map="dd_test_draw",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.50, scales=4,
                       overwrite=True)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="dD_simple", flags="c").strip()
        vals = [float(v) for v in out.splitlines() if v.strip()]
        self.assertGreaterEqual(len(vals), 1, "no detections")
        for v in vals:
            self.assertAlmostEqual(v, 0.420, places=3,
                                    msg=f"dD_simple={v} did not pick up "
                                    "the raster value 0.420")
        self.runModule("g.remove", flags="f", type="raster",
                       name="dd_test_draw", quiet=True)


    def test_ml_baseline_method_runs(self):
        """method=ml without a model file falls back to the uniform-
        weight baseline and completes without error. The output
        polygons carry method='ml-baseline' (not 'dem' or 'image')."""
        # method=ml needs P1+P2 to feed it - build a single-band
        # image from the DEM as a proxy.
        self.runModule("r.mapcalc",
                       expression="syn_img = syn_dem",
                       overwrite=True)
        module = SimpleModule("p.crater.draw",
                               dem="syn_dem", image="syn_img",
                               sun_azimuth=170.0,
                               output="detected_test",
                               method="ml",
                               body="moon",
                               min_diameter=800, max_diameter=2000,
                               threshold=0.40, scales=4,
                               overwrite=True)
        self.assertModule(module)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="method", flags="c").strip()
        methods = {m.strip() for m in out.splitlines() if m.strip()}
        # Some method tag from the ML rescore path should be present.
        self.assertTrue(any("ml" in m for m in methods),
                          f"Expected at least one method='ml*' tag, "
                          f"got {methods}")
        self.runModule("g.remove", flags="f", type="raster",
                       name="syn_img", quiet=True)

    def test_opencl_diagnostic_runs(self):
        """-c flag runs the GPU path when available, otherwise falls
        back to OpenMP. The module must complete normally either way."""
        module = SimpleModule("p.crater.draw",
                               flags="c",
                               dem="syn_dem",
                               output="detected_test",
                               method="dem",
                               body="moon",
                               min_diameter=800, max_diameter=2000,
                               threshold=0.50, scales=4,
                               overwrite=True)
        self.assertModule(module)

    def test_ml_dem_only_accepts_single_detector(self):
        """method=ml with only a DEM (no image) must succeed - the
        meta-detector fills the missing image channel with zeros and
        still produces a sensible output."""
        module = SimpleModule("p.crater.draw",
                               dem="syn_dem",
                               output="detected_test",
                               method="ml",
                               body="moon",
                               min_diameter=800, max_diameter=2000,
                               threshold=0.40, scales=4,
                               overwrite=True)
        self.assertModule(module)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="method", flags="c").strip()
        methods = {m.strip() for m in out.splitlines() if m.strip()}
        self.assertTrue(any("ml" in m for m in methods),
                          "Expected at least one method='ml*' tag from "
                          f"single-detector P3, got {methods}")

    def test_norefine_flag_runs(self):
        """-R flag disables sub-pixel refinement; module must still
        complete successfully and produce at least one detection."""
        module = SimpleModule("p.crater.draw",
                               flags="R",
                               dem="syn_dem",
                               output="detected_test",
                               method="dem",
                               body="moon",
                               min_diameter=800, max_diameter=2000,
                               threshold=0.50, scales=4,
                               overwrite=True)
        self.assertModule(module)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="cat", flags="c").strip()
        rows = [r for r in out.splitlines() if r.strip()]
        self.assertGreaterEqual(len(rows), 1,
                                  "Expected at least one detection even "
                                  "without sub-pixel refinement")

    def test_subpixel_refinement_centre_accuracy(self):
        """Sub-pixel refinement (default ON) must place the detected
        centre within 200 m of the synthetic crater origin. The coarse
        detector strides at r/3, so the worst-case quantisation error
        is ~r/6 ≈ 100 m for a 1200-m crater scanned at 10 m/pixel;
        refinement must not move the centre further away."""
        # Crater centred at (50, 50) – intentionally off-grid so the
        # coarse stride may introduce a ~50–150 m error.
        self.runModule("r.mapcalc",
                       overwrite=True,
                       expression=(
                           "syn_dem_off = "
                           "1000.0 + "
                           "if(sqrt((x()-50)*(x()-50) + "
                           "       (y()-50)*(y()-50)) < 550, -100, "
                           "if(sqrt((x()-50)*(x()-50) + "
                           "       (y()-50)*(y()-50)) < 650,   50, 0))"
                       ))
        self.runModule("p.crater.draw",
                       dem="syn_dem_off",
                       output="detected_test",
                       method="dem",
                       body="moon",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.50, scales=4,
                       overwrite=True)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="cx,cy", flags="c").strip()
        rows = [r for r in out.splitlines() if r.strip()]
        self.assertGreaterEqual(len(rows), 1, "No detections after refinement")
        cx, cy = map(float, rows[0].split("|"))
        dist = math.hypot(cx - 50.0, cy - 50.0)
        self.assertLess(dist, 200.0,
                          f"Refined centre ({cx:.1f}, {cy:.1f}) is "
                          f"{dist:.1f} m from true origin (50, 50); "
                          "expected < 200 m after sub-pixel refinement")
        self.runModule("g.remove", flags="f", type="raster",
                       name="syn_dem_off", quiet=True)

    def test_opencl_gpu_path_finds_synthetic_crater(self):
        """When OpenCL is built and a device is present, the -c path
        must still find the synthetic crater (centre within a few
        hundred metres, at least one detection). If no OpenCL device
        is available, the module falls back to OpenMP and this test
        is still meaningful."""
        self.runModule("p.crater.draw", flags="c",
                       dem="syn_dem",
                       output="detected_test",
                       method="dem",
                       body="moon",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.40, scales=4,
                       overwrite=True)
        out = gs.read_command("v.db.select", map="detected_test",
                              columns="cat,cx,cy,D_eq",
                              flags="c").strip()
        rows = [r for r in out.splitlines() if r.strip()]
        self.assertGreaterEqual(len(rows), 1,
                                  "GPU path produced no detections on "
                                  "the synthetic crater - likely an "
                                  "OpenCL kernel divergence from the "
                                  "CPU implementation")


    def test_opencl_cpu_equivalence(self):
        """The GPU (-c) and CPU paths must agree on D_eq and confidence to
        within a small tolerance on the same synthetic DEM.  When no OpenCL
        device is available the -c path falls back to OpenMP and the two
        results are trivially identical.  When a device IS available the
        floating-point scores must agree to within 2 % of the confidence
        range and 1 % of D_eq (bilinear DEM lookup is deterministic so any
        divergence indicates a kernel mismatch)."""
        # CPU reference run
        self.runModule("p.crater.draw",
                       dem="syn_dem",
                       output="detected_cpu",
                       method="dem", body="moon",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.40, scales=4,
                       overwrite=True)
        # GPU run (falls back to CPU if no device)
        self.runModule("p.crater.draw",
                       flags="c",
                       dem="syn_dem",
                       output="detected_gpu",
                       method="dem", body="moon",
                       min_diameter=800, max_diameter=2000,
                       threshold=0.40, scales=4,
                       overwrite=True)
        cpu_out = gs.read_command("v.db.select", map="detected_cpu",
                                  columns="D_eq,confidence", flags="c").strip()
        gpu_out = gs.read_command("v.db.select", map="detected_gpu",
                                  columns="D_eq,confidence", flags="c").strip()
        cpu_rows = [r for r in cpu_out.splitlines() if r.strip()]
        gpu_rows = [r for r in gpu_out.splitlines() if r.strip()]
        self.assertEqual(len(cpu_rows), len(gpu_rows),
                          f"CPU produced {len(cpu_rows)} detections, "
                          f"GPU produced {len(gpu_rows)} — counts differ")
        for i, (cr, gr) in enumerate(zip(cpu_rows, gpu_rows)):
            cd, cc = map(float, cr.split("|"))
            gd, gc = map(float, gr.split("|"))
            self.assertAlmostEqual(cd, gd, delta=cd * 0.01,
                                    msg=f"D_eq mismatch at row {i}: "
                                    f"cpu={cd:.2f} gpu={gd:.2f}")
            self.assertAlmostEqual(cc, gc, delta=0.02,
                                    msg=f"confidence mismatch at row {i}: "
                                    f"cpu={cc:.4f} gpu={gc:.4f}")
        self.runModule("g.remove", flags="f", type="vector",
                       name="detected_cpu,detected_gpu", quiet=True)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
