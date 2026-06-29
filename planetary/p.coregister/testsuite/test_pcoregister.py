"""Tests for p.coregister — phase correlation and NCC co-registration."""

import os
import math
import tempfile
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


def _shift_report(path):
    """Parse a shift report CSV: return (dx_pix, dy_pix, dx_m, dy_m)."""
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 4:
                return (float(parts[0]), float(parts[1]),
                        float(parts[2]), float(parts[3]))
    return None


class TestCoregister(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        # 64×64 at 1 m/px
        gs.run_command("g.region", rows=64, cols=64,
                       n=64, s=0, e=64, w=0, nsres=1, ewres=1)
        # Textured master: multi-frequency sinusoids with periods that DIVIDE 64
        # evenly (8, 16, 4) so the DFT has exact integer-bin frequencies.
        # This is required for phase correlation without Hann window; real imagery
        # has broad spectral content and benefits from the Hann window instead.
        pi = "3.14159265"
        expr_m = (f"coreg_master = sin(2*{pi}*row()/8) + cos(2*{pi}*row()/16)"
                  f" + cos(2*{pi}*col()/8) + sin(2*{pi}*col()/4)")
        gs.run_command("r.mapcalc", expression=expr_m, overwrite=True)
        # Slave: same pattern shifted 5 px east (col-5), 3 px south (row-3).
        expr_s = (f"coreg_slave = sin(2*{pi}*(row()-3)/8) + cos(2*{pi}*(row()-3)/16)"
                  f" + cos(2*{pi}*(col()-5)/8) + sin(2*{pi}*(col()-5)/4)")
        gs.run_command("r.mapcalc", expression=expr_s, overwrite=True)
        # Zero-shift slave (identical to master)
        gs.run_command("r.mapcalc",
                       expression="coreg_slave0 = coreg_master * 1.0",
                       overwrite=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       pattern="coreg_*",
                       flags="f", quiet=True)

    # ── basic execution ───────────────────────────────────────────────────────
    def test_runs_without_error(self):
        """p.coregister runs without error on synthetic data."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave",
                              output="coreg_out", report=rep, overwrite=True)
            self.assertTrue(os.path.exists(rep))
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out",
                       flags="f", quiet=True)

    # ── phase correlation accuracy ────────────────────────────────────────────
    def test_shift_recovered_dx(self):
        """Phase correlation recovers dx=5 px (east shift) within ±1 px.
        Uses -w (no Hann window) because the test signal is DFT-aligned;
        Hann window is recommended for real imagery with spectral leakage."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave",
                              output="coreg_out_dx", report=rep,
                              flags="w", overwrite=True)
            result = _shift_report(rep)
            self.assertIsNotNone(result)
            dx_pix = result[0]
            self.assertAlmostEqual(dx_pix, 5.0, delta=1.0,
                                   msg=f"Expected dx≈5, got {dx_pix}")
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out_dx",
                       flags="f", quiet=True)

    def test_shift_recovered_dy(self):
        """Phase correlation recovers dy=3 px (south shift) within ±1 px."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave",
                              output="coreg_out_dy", report=rep,
                              flags="w", overwrite=True)
            result = _shift_report(rep)
            self.assertIsNotNone(result)
            dy_pix = result[1]
            self.assertAlmostEqual(dy_pix, 3.0, delta=1.0,
                                   msg=f"Expected dy≈3, got {dy_pix}")
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out_dy",
                       flags="f", quiet=True)

    def test_zero_shift_near_zero(self):
        """Identical rasters yield near-zero shift (|dx|<1, |dy|<1)."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave0",
                              output="coreg_out_z", report=rep,
                              flags="w", overwrite=True)
            result = _shift_report(rep)
            self.assertIsNotNone(result)
            dx_pix, dy_pix = result[0], result[1]
            self.assertLess(abs(dx_pix), 1.0,
                            f"Zero-shift dx should be <1, got {dx_pix}")
            self.assertLess(abs(dy_pix), 1.0,
                            f"Zero-shift dy should be <1, got {dy_pix}")
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out_z",
                       flags="f", quiet=True)

    def test_map_unit_shift_correct(self):
        """Map-unit shift = pixel shift × resolution (ewres=nsres=1 m here)."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave",
                              output="coreg_out_mu", report=rep,
                              flags="w", overwrite=True)
            result = _shift_report(rep)
            dx_pix, dy_pix, dx_m, dy_m = result
            self.assertAlmostEqual(dx_m, dx_pix, places=5)
            self.assertAlmostEqual(dy_m, dy_pix, places=5)
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out_mu",
                       flags="f", quiet=True)

    # ── NCC refinement mode ───────────────────────────────────────────────────
    def test_ncc_mode_runs(self):
        """NCC refinement (-n) runs without error."""
        fd, rep = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.coregister",
                              master="coreg_master", slave="coreg_slave",
                              output="coreg_out_ncc", report=rep,
                              search=3, flags="n", overwrite=True)
            with open(rep) as f:
                content = f.read()
            self.assertIn("ncc_refined", content)
        finally:
            os.remove(rep)
        gs.run_command("g.remove", type="raster", name="coreg_out_ncc",
                       flags="f", quiet=True)

    # ── output raster validity ────────────────────────────────────────────────
    def test_output_raster_has_data(self):
        """Registered output raster has non-zero data pixels."""
        self.assertModule("p.coregister",
                          master="coreg_master", slave="coreg_slave",
                          output="coreg_out_chk", overwrite=True)
        info = gs.parse_command("r.univar", map="coreg_out_chk",
                                 flags="g", quiet=True)
        self.assertGreater(int(info.get("n", 0)), 0)
        gs.run_command("g.remove", type="raster", name="coreg_out_chk",
                       flags="f", quiet=True)

    def test_no_hann_flag(self):
        """-w (no Hann window) still produces a valid output."""
        self.assertModule("p.coregister",
                          master="coreg_master", slave="coreg_slave",
                          output="coreg_out_nohann", flags="w", overwrite=True)
        info = gs.parse_command("r.univar", map="coreg_out_nohann",
                                 flags="g", quiet=True)
        self.assertGreater(int(info.get("n", 0)), 0)
        gs.run_command("g.remove", type="raster", name="coreg_out_nohann",
                       flags="f", quiet=True)


if __name__ == "__main__":
    test()
