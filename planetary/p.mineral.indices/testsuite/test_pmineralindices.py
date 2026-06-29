"""Tests for p.mineral.indices — all bodies and index types."""

import os
import math
import tempfile
import shutil
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestMineralIndices(TestCase):

    WL_CSV = None   # path to a temp wavelength CSV

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", rows=10, cols=10)

        # 20-band synthetic raster: constant reflectance profile
        # band b has value b/20.0  → monotonically increasing from 0.05 to 1.0
        for b in range(1, 21):
            gs.run_command("r.mapcalc",
                           expression=f"synth.{b} = {b / 20.0:.4f}",
                           overwrite=True)

        # Wavelength CSV: 20 bands at 100 nm spacing starting at 0.40 µm
        fd, cls.WL_CSV = tempfile.mkstemp(suffix=".csv")
        with os.fdopen(fd, "w") as f:
            for b in range(20):
                f.write(f"{0.40 + b * 0.10:.2f}\n")

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       pattern="synth.*,idx_out",
                       flags="f", quiet=True)
        if cls.WL_CSV and os.path.exists(cls.WL_CSV):
            os.remove(cls.WL_CSV)

    # ── Generic (any body) ────────────────────────────────────────────────────
    def _run(self, body, index):
        gs.run_command("p.mineral.indices",
                       body=body, input="synth", output="idx_out",
                       index=index, wavelengths=self.WL_CSV,
                       overwrite=True)
        return gs.parse_command("r.univar", map="idx_out", flags="g", quiet=True)

    def test_olivine_runs(self):
        """olivine BD runs without error for body=mars."""
        info = self._run("mars", "olivine")
        self.assertGreater(int(info.get("n", 0)), 0)

    def test_pyroxene_runs(self):
        """pyroxene BD runs without error."""
        info = self._run("mars", "pyroxene")
        self.assertGreater(int(info.get("n", 0)), 0)

    def test_tio2_ratio(self):
        """tio2 ratio is finite and positive."""
        info = self._run("mars", "tio2")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_feo_ratio(self):
        """feo ratio is finite and positive."""
        info = self._run("mars", "feo")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_mafic_ibd(self):
        """mafic IBD runs without error."""
        info = self._run("mars", "mafic")
        self.assertGreater(int(info.get("n", 0)), 0)

    # ── Moon / M3 ─────────────────────────────────────────────────────────────
    def test_ibd1000(self):
        """M3 IBD1000 runs without error for body=moon."""
        info = self._run("moon", "ibd1000")
        self.assertGreater(int(info.get("n", 0)), 0)

    def test_ibd2000(self):
        """M3 IBD2000 runs without error for body=moon."""
        info = self._run("moon", "ibd2000")
        self.assertGreater(int(info.get("n", 0)), 0)

    def test_r1580_1250(self):
        """M3 hydroxyl ratio is finite and positive."""
        info = self._run("moon", "r1580_1250")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_bd2800(self):
        """M3 BD(2800) runs without error."""
        info = self._run("moon", "bd2800")
        self.assertGreater(int(info.get("n", 0)), 0)

    # ── Mercury / MDIS ────────────────────────────────────────────────────────
    def test_r749_433(self):
        """MDIS maturity index is finite and positive."""
        info = self._run("mercury", "r749_433")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_r996_749(self):
        """MDIS mafic index is finite and positive."""
        info = self._run("mercury", "r996_749")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_spec_slope(self):
        """MDIS spectral slope is finite (positive for monotonically rising spectrum)."""
        info = self._run("mercury", "spec_slope")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        # Monotonically rising synthetic spectrum → positive slope
        self.assertGreater(mean, 0)

    # ── Titan / VIMS ──────────────────────────────────────────────────────────
    def test_r500_200(self):
        """Titan 5.0/2.0 µm ratio: not all null (wavelengths extend to 2.3 µm in test)."""
        # Our test grid only goes to 2.3 µm — both bands are outside, so result
        # will be NaN/null. Just check the module runs without error.
        ret = gs.run_command("p.mineral.indices",
                             body="titan", input="synth", output="idx_out",
                             index="r500_200", wavelengths=self.WL_CSV,
                             overwrite=True)
        self.assertEqual(ret, 0)

    def test_r159_127(self):
        """Titan 1.59/1.27 µm ratio is finite (both within test grid)."""
        info = self._run("titan", "r159_127")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    # ── Venus / VIRTIS ────────────────────────────────────────────────────────
    def test_r1740_1300(self):
        """Venus 1.74/1.30 µm emission window ratio is finite and positive."""
        info = self._run("venus", "r1740_1300")
        mean = float(info.get("mean", "nan"))
        self.assertTrue(math.isfinite(mean))
        self.assertGreater(mean, 0)

    def test_bd2300(self):
        """Venus BD(2.3µm) runs without error."""
        info = self._run("venus", "bd2300")
        self.assertGreater(int(info.get("n", 0)), 0)

    # ── Body/index mismatch produces fatal error ──────────────────────────────
    def test_moon_index_on_mars_fails(self):
        """ibd1000 on body=mars should fail (body mismatch)."""
        import subprocess, sys
        ret = gs.run_command("p.mineral.indices",
                             body="mars", input="synth", output="idx_out",
                             index="ibd1000", wavelengths=self.WL_CSV,
                             overwrite=True, run_=False)
        self.assertNotEqual(ret.returncode if hasattr(ret, 'returncode') else 0, 0)


if __name__ == "__main__":
    test()
