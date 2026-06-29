"""
Tests for p.spec.pca — PCA of multi-band planetary rasters.

Tests use synthetic rasters (created with r.mapcalc) with known structure:
  band.1 = col * 1.0       (pure along-column gradient)
  band.2 = col * 1.0 + row (correlated + row component)
  band.3 = row * 1.0       (pure along-row gradient)

Expected: PC-1 captures the shared gradient (largest variance), PC-3 the
residual.  Eigenvalue CSV must list PC-1 variance > PC-2 > PC-3.
"""

import os
import math
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestSpecPCA(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", rows=20, cols=20)
        # Synthetic 3-band raster with known covariance structure
        for b, expr in [("1", "col() * 1.0"),
                        ("2", "col() * 1.0 + row() * 0.5"),
                        ("3", "row() * 1.0")]:
            gs.run_command("r.mapcalc",
                           expression=f"test_band.{b} = {expr}",
                           overwrite=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster", pattern="test_band.*",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="raster", pattern="test_pc.*",
                       flags="f", quiet=True)
        if os.path.exists("test_pca_stats.csv"):
            os.remove("test_pca_stats.csv")

    def test_basic_pca_runs(self):
        """p.spec.pca runs without error on 3-band synthetic data."""
        ret = gs.run_command("p.spec.pca",
                             input="test_band",
                             output="test_pc",
                             ncomps=3,
                             stats="test_pca_stats.csv",
                             overwrite=True)
        self.assertEqual(ret, 0)

    def test_output_rasters_created(self):
        """Output PC rasters test_pc.1, .2, .3 are created."""
        gs.run_command("p.spec.pca",
                       input="test_band", output="test_pc",
                       ncomps=3, overwrite=True)
        for k in range(1, 4):
            self.assertRasterExists(f"test_pc.{k}")

    def test_eigenvalues_descending(self):
        """Eigenvalue CSV reports descending eigenvalues."""
        gs.run_command("p.spec.pca",
                       input="test_band", output="test_pc",
                       ncomps=3, stats="test_pca_stats.csv",
                       overwrite=True)
        self.assertTrue(os.path.exists("test_pca_stats.csv"))
        evals = []
        with open("test_pca_stats.csv") as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.strip().split(",")
                evals.append(float(parts[1]))
        self.assertGreater(len(evals), 1)
        for i in range(len(evals) - 1):
            self.assertGreaterEqual(evals[i], evals[i + 1])

    def test_pc1_has_highest_variance(self):
        """PC-1 has strictly higher variance than PC-2 for this dataset."""
        gs.run_command("p.spec.pca",
                       input="test_band", output="test_pc",
                       ncomps=3, stats="test_pca_stats.csv",
                       overwrite=True)
        evals = []
        with open("test_pca_stats.csv") as f:
            for line in f:
                if line.startswith("#"):
                    continue
                evals.append(float(line.strip().split(",")[1]))
        self.assertGreater(evals[0], evals[1])

    def test_ncomps_limits_output(self):
        """ncomps=2 creates only .1 and .2, not .3."""
        gs.run_command("p.spec.pca",
                       input="test_band", output="test_pc",
                       ncomps=2, overwrite=True)
        self.assertRasterExists("test_pc.1")
        self.assertRasterExists("test_pc.2")
        self.assertFalse(
            gs.find_file("test_pc.3", element="cell")["name"],
            "test_pc.3 should not exist when ncomps=2"
        )

    def test_pc_scores_not_all_null(self):
        """PC rasters contain non-null values."""
        gs.run_command("p.spec.pca",
                       input="test_band", output="test_pc",
                       ncomps=3, overwrite=True)
        info = gs.parse_command("r.univar", map="test_pc.1",
                                flags="g", quiet=True)
        n = int(info.get("n", 0))
        self.assertGreater(n, 0)

    def test_standardised_flag_runs(self):
        """-s (correlation-matrix PCA) flag runs without error."""
        ret = gs.run_command("p.spec.pca",
                             input="test_band", output="test_pc",
                             ncomps=3, flags="s", overwrite=True)
        self.assertEqual(ret, 0)
        self.assertRasterExists("test_pc.1")


if __name__ == "__main__":
    test()
