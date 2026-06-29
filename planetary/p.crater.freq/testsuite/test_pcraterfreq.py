"""Tests for p.crater.freq — production functions, R-plot, Poisson, Titan."""

import os
import math
import tempfile
import shutil
import random
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


def _make_csv(n=200, dmin=0.1, dmax=50.0, seed=42):
    """Write a power-law crater population CSV, return path."""
    rng = random.Random(seed)
    fd, path = tempfile.mkstemp(suffix=".csv")
    with os.fdopen(fd, "w") as f:
        f.write("# synthetic craters [km]\n")
        for _ in range(n * 4):
            u = rng.random()
            d = dmin * (1 - u) ** (-0.5)
            if dmin <= d <= dmax:
                f.write(f"{d:.4f}\n")
    return path


class TestCraterFreq(TestCase):

    CSV = None

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)
        cls.CSV = _make_csv()

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        if cls.CSV and os.path.exists(cls.CSV):
            os.remove(cls.CSV)

    # ── basic N-mode ──────────────────────────────────────────────────────────
    def test_mars_runs(self):
        """p.crater.freq runs for body=mars without error."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="mars", nbins=10)

    def test_moon_runs(self):
        """p.crater.freq runs for body=moon."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="moon", nbins=10)

    def test_mercury_runs(self):
        """p.crater.freq runs for body=mercury."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="mercury", nbins=10)

    def test_vesta_runs(self):
        """p.crater.freq runs for body=vesta."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="vesta", nbins=10)

    # ── Titan production function ──────────────────────────────────────────────
    def test_titan_runs(self):
        """p.crater.freq runs for body=titan (Artemieva & Lunine 2003 NPF)."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="titan", nbins=10)

    def test_titan_older_than_moon(self):
        """Titan gives slightly older age than Moon for same data (lower flux)."""
        fd_m, out_moon  = tempfile.mkstemp(suffix=".csv")
        fd_t, out_titan = tempfile.mkstemp(suffix=".csv")
        os.close(fd_m); os.close(fd_t)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="moon",
                              nbins=10, output=out_moon)
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="titan",
                              nbins=10, output=out_titan)
            # Read age from header comment "age=X.XXXX Ga"
            def read_age(path):
                with open(path) as f:
                    for line in f:
                        if "age=" in line:
                            tok = [t for t in line.split() if t.startswith("age=")]
                            if tok:
                                return float(tok[0].split("=")[1])
                return None
            age_moon  = read_age(out_moon)
            age_titan = read_age(out_titan)
            self.assertIsNotNone(age_moon)
            self.assertIsNotNone(age_titan)
            self.assertGreater(age_titan, age_moon,
                               "Titan (0.5x flux) should give older age than Moon")
        finally:
            os.remove(out_moon)
            os.remove(out_titan)

    # ── R-plot mode ───────────────────────────────────────────────────────────
    def test_rplot_produces_output(self):
        """R-plot mode (-r) writes R_obs and R_sat columns."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=10, output=out, flags="r")
            with open(out) as f:
                content = f.read()
            self.assertIn("R_obs", content)
            self.assertIn("R_sat", content)
            # R_sat = 0.4 (constant) should appear in data rows
            self.assertIn("4.000000e-01", content)
        finally:
            os.remove(out)

    def test_rplot_R_sat_constant(self):
        """All R_sat values in R-plot output equal 0.4."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=8, output=out, flags="r")
            r_sat_vals = []
            with open(out) as f:
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    cols = line.split()
                    if len(cols) >= 6:
                        try:
                            r_sat_vals.append(float(cols[5]))
                        except ValueError:
                            pass
            self.assertTrue(len(r_sat_vals) > 0, "No R_sat data rows found")
            for v in r_sat_vals:
                self.assertAlmostEqual(v, 0.4, places=5,
                                       msg=f"R_sat should be 0.4, got {v}")
        finally:
            os.remove(out)

    # ── Poisson uncertainty ───────────────────────────────────────────────────
    def test_poisson_columns(self):
        """Poisson flag (-p) adds sigma column to N-mode output."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=10, output=out, flags="p")
            with open(out) as f:
                content = f.read()
            self.assertIn("sigma_N", content)
        finally:
            os.remove(out)

    def test_poisson_rplot_columns(self):
        """Combined -rp produces R_obs + sigma_R columns."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=10, output=out, flags="rp")
            with open(out) as f:
                content = f.read()
            self.assertIn("sigma_R", content)
            self.assertIn("R_obs", content)
        finally:
            os.remove(out)

    def test_age_uncertainty_in_header(self):
        """Age ± sigma appears in output CSV header when craters >= 1 km exist."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=12, output=out)
            with open(out) as f:
                header = f.read(500)
            self.assertIn("+/-", header, "Age uncertainty (+/-) missing from header")
        finally:
            os.remove(out)

    # ── saturation column in default mode ─────────────────────────────────────
    def test_n_sat_in_default_output(self):
        """N_sat column is always present in default (N-mode) output."""
        fd, out = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self.assertModule("p.crater.freq",
                              input=self.CSV, area=10000, body="mars",
                              nbins=10, output=out)
            with open(out) as f:
                content = f.read()
            self.assertIn("N_sat", content)
        finally:
            os.remove(out)

    # ── Hartmann chi-square fit still works ───────────────────────────────────
    def test_hartmann_mars(self):
        """Hartmann isochron fit (-t) for Mars runs without error."""
        self.assertModule("p.crater.freq",
                          input=self.CSV, area=10000, body="mars",
                          nbins=10, flags="t")


if __name__ == "__main__":
    test()
