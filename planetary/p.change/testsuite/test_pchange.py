"""Tests for p.change — temporal change detection."""

import os
import math
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestChange(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", rows=64, cols=64,
                       n=64, s=0, e=64, w=0, nsres=1, ewres=1)
        # Flat reference raster (value = 10.0 everywhere)
        gs.run_command("r.mapcalc", expression="ch_a = 10.0", overwrite=True)
        # Later raster: half the image increased by 5 (ch = 15 east, 10 west)
        gs.run_command("r.mapcalc",
                       expression="ch_b = if(col() > 32, 15.0, 10.0)",
                       overwrite=True)
        # Ratio-friendly pair: b = 2*a everywhere
        gs.run_command("r.mapcalc", expression="ch_a2 = 5.0", overwrite=True)
        gs.run_command("r.mapcalc", expression="ch_b2 = 10.0", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster", pattern="ch_*",
                       flags="f", quiet=True)

    # ── basic execution ───────────────────────────────────────────────────────
    def test_runs_without_error(self):
        """p.change runs without error (default difference mode)."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_out_basic", overwrite=True)
        gs.run_command("g.remove", type="raster", name="ch_out_basic",
                       flags="f", quiet=True)

    # ── difference mode ───────────────────────────────────────────────────────
    def test_difference_values(self):
        """Difference (b-a): eastern half = +5, western half = 0."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_diff", mode="difference", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_diff", flags="g",
                                 quiet=True)
        vmax = float(stats["max"])
        vmin = float(stats["min"])
        self.assertAlmostEqual(vmax, 5.0, places=3,
                               msg=f"Difference max should be 5.0, got {vmax}")
        self.assertAlmostEqual(vmin, 0.0, places=3,
                               msg=f"Difference min should be 0.0, got {vmin}")
        gs.run_command("g.remove", type="raster", name="ch_diff",
                       flags="f", quiet=True)

    # ── ratio mode ────────────────────────────────────────────────────────────
    def test_ratio_values(self):
        """Ratio (b/a): uniform 5→10 raster should give ratio=2 everywhere."""
        self.assertModule("p.change",
                          input_a="ch_a2", input_b="ch_b2",
                          output="ch_ratio", mode="ratio", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_ratio", flags="g",
                                 quiet=True)
        mean = float(stats["mean"])
        self.assertAlmostEqual(mean, 2.0, places=3,
                               msg=f"Ratio mean should be 2.0, got {mean}")
        gs.run_command("g.remove", type="raster", name="ch_ratio",
                       flags="f", quiet=True)

    # ── normalised difference mode ────────────────────────────────────────────
    def test_ndiff_values(self):
        """ndiff (b-a)/(b+a): 5→10 should give 1/3 everywhere."""
        self.assertModule("p.change",
                          input_a="ch_a2", input_b="ch_b2",
                          output="ch_ndiff", mode="ndiff", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_ndiff", flags="g",
                                 quiet=True)
        mean = float(stats["mean"])
        self.assertAlmostEqual(mean, 1.0 / 3.0, places=3,
                               msg=f"ndiff mean should be 1/3, got {mean}")
        gs.run_command("g.remove", type="raster", name="ch_ndiff",
                       flags="f", quiet=True)

    def test_ndiff_range(self):
        """ndiff result is always in [-1, +1]."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_ndiff2", mode="ndiff", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_ndiff2", flags="g",
                                 quiet=True)
        self.assertGreaterEqual(float(stats["min"]), -1.0)
        self.assertLessEqual(float(stats["max"]), 1.0)
        gs.run_command("g.remove", type="raster", name="ch_ndiff2",
                       flags="f", quiet=True)

    # ── log_ratio mode ────────────────────────────────────────────────────────
    def test_log_ratio_values(self):
        """log_ratio ln(b/a): 5→10 should give ln(2) everywhere."""
        self.assertModule("p.change",
                          input_a="ch_a2", input_b="ch_b2",
                          output="ch_lr", mode="log_ratio", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_lr", flags="g",
                                 quiet=True)
        mean = float(stats["mean"])
        self.assertAlmostEqual(mean, math.log(2.0), places=3,
                               msg=f"log_ratio mean should be ln(2)={math.log(2):.4f}, got {mean}")
        gs.run_command("g.remove", type="raster", name="ch_lr",
                       flags="f", quiet=True)

    # ── absolute threshold masking ────────────────────────────────────────────
    def test_threshold_masks_unchanged(self):
        """threshold=4.0: western half (diff=0) masked to NULL."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_thr", mode="difference",
                          threshold="4.0", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_thr", flags="g",
                                 quiet=True)
        n_valid = int(stats["n"])
        # Eastern half (col>32 of 64 cols) has change=5>4 → kept
        # Western half (col<=32) has change=0<4 → masked; 32*64=2048 masked
        self.assertLess(n_valid, 64 * 64,
                        "Threshold should have masked some pixels to NULL")
        self.assertGreater(n_valid, 0, "Threshold should have kept some pixels")
        gs.run_command("g.remove", type="raster", name="ch_thr",
                       flags="f", quiet=True)

    # ── relative threshold masking ────────────────────────────────────────────
    def test_rel_threshold(self):
        """rel_threshold=0.3: ch_b eastern (diff/ref=0.5>0.3) kept, west masked."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_rel", mode="difference",
                          rel_threshold="0.3", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_rel", flags="g",
                                 quiet=True)
        n_valid = int(stats["n"])
        self.assertLess(n_valid, 64 * 64)
        self.assertGreater(n_valid, 0)
        gs.run_command("g.remove", type="raster", name="ch_rel",
                       flags="f", quiet=True)

    # ── binary mask output ────────────────────────────────────────────────────
    def test_binary_mask_values(self):
        """-m flag: output is a binary raster with only 0 and 1 values."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_mask", mode="difference",
                          threshold="4.0", flags="m", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_mask", flags="g",
                                 quiet=True)
        vmin = float(stats["min"])
        vmax = float(stats["max"])
        self.assertAlmostEqual(vmin, 0.0, places=3)
        self.assertAlmostEqual(vmax, 1.0, places=3)
        gs.run_command("g.remove", type="raster", name="ch_mask",
                       flags="f", quiet=True)

    # ── de-mean flag ──────────────────────────────────────────────────────────
    def test_demean_flag(self):
        """-s flag: de-meaned difference map has mean near zero."""
        self.assertModule("p.change",
                          input_a="ch_a", input_b="ch_b",
                          output="ch_dm", mode="difference",
                          flags="s", overwrite=True)
        stats = gs.parse_command("r.univar", map="ch_dm", flags="g",
                                 quiet=True)
        mean = float(stats["mean"])
        self.assertAlmostEqual(mean, 0.0, delta=0.1,
                               msg=f"De-meaned output mean should be ≈0, got {mean}")
        gs.run_command("g.remove", type="raster", name="ch_dm",
                       flags="f", quiet=True)


if __name__ == "__main__":
    test()
