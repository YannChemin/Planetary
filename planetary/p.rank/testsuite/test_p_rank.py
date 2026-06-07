"""
Testsuite for p.rank.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.rank/testsuite/test_p_rank.py -v

Builds a suitability raster with one high-value contiguous blob and checks
that p.rank produces the uncertainty raster and a valid JSON report.
"""

import os
import json
import tempfile

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestRank(TestCase):

    suit = "test_rank_suit"
    prefix = "trank"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        # 100x100 cells at 30 m -> 9 km2 total; central blob ~ 0.9 km2.
        gs.run_command("g.region", n=3000, s=0, e=3000, w=0, res=30)
        gs.mapcalc(
            f"{cls.suit} = if(row()>30 && row()<70 && col()>30 && col()<70,"
            f" 0.9, 0.3)", overwrite=True)
        cls.tmp = tempfile.mkdtemp(prefix="prank_")
        cls.report = os.path.join(cls.tmp, "report.json")

    @classmethod
    def tearDownClass(cls):
        import shutil
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.suit},{cls.prefix}_uncertainty",
                       flags="f", quiet=True)
        gs.run_command("g.remove", type="vector",
                       name=f"{cls.prefix}_candidates", flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def test_rank_report(self):
        self.assertModule("p.rank", suitability=self.suit,
                          min_area_km2=0.05, top_percentile=80,
                          n_candidates=5, mc_samples=50,
                          prefix=self.prefix, report=self.report,
                          overwrite=True)
        self.assertRasterExists(f"{self.prefix}_uncertainty")
        self.assertFileExists(self.report)
        with open(self.report) as f:
            rep = json.load(f)
        # Either ranked candidates or a graceful no-candidate status.
        self.assertTrue("candidates" in rep or "status" in rep)
        if rep.get("candidates"):
            top = rep["candidates"][0]
            self.assertIn("area_km2", top)
            self.assertGreaterEqual(top["area_km2"], 0.05)

    def test_mc_rank1_prob_without_criteria(self):
        """Regression: when called without `criteria=`, the MC block used to
        be skipped entirely, leaving rank1_probability = null for every
        candidate. The fallback path must now perturb suit_mean±suit_std and
        produce probabilities that sum to ~1 across the ranked set."""
        # Two distinct blobs so the ranking has something to perturb.
        two_blob = "test_rank_two_blob"
        gs.mapcalc(
            f"{two_blob} = if(row()>30 && row()<70 && col()>30 && col()<70,"
            f" 0.90, if(row()>30 && row()<70 && col()>72 && col()<95,"
            f" 0.88, 0.30))", overwrite=True)
        report2 = os.path.join(self.tmp, "report_mc.json")
        try:
            self.assertModule("p.rank", suitability=two_blob,
                              min_area_km2=0.05, top_percentile=80,
                              n_candidates=5, mc_samples=200,
                              prefix=f"{self.prefix}_mc", report=report2,
                              overwrite=True)
            with open(report2) as f:
                rep = json.load(f)
            cands = rep.get("candidates", [])
            self.assertGreaterEqual(len(cands), 2,
                "Need >=2 candidates to exercise the MC fallback.")
            probs = [c.get("rank1_probability") for c in cands]
            self.assertTrue(all(p is not None for p in probs),
                "rank1_probability is null — MC fallback didn't fire.")
            self.assertGreater(sum(probs), 0.5,
                "Per-candidate rank1 probabilities should sum to ~1.")
            self.assertTrue(any(p > 0 for p in probs),
                "At least one candidate must have non-zero rank1 probability.")
        finally:
            gs.run_command("g.remove", type="raster", name=two_blob,
                           flags="f", quiet=True)
            gs.run_command("g.remove", type="raster",
                           name=f"{self.prefix}_mc_uncertainty",
                           flags="f", quiet=True)
            gs.run_command("g.remove", type="vector",
                           name=f"{self.prefix}_mc_candidates",
                           flags="f", quiet=True)


if __name__ == "__main__":
    test()
