"""
Testsuite for p.rank.cross.

Run with:
    grass --tmp-project XY --exec python3 -m pytest \
        p.rank.cross/testsuite/test_p_rank_cross.py -v

p.rank.cross is pure JSON-in / JSON-out, so it runs fine in an XY project.
The tests synthesise two minimal p.rank-style reports, exercise the
module, and verify the cross ranking against hand-computed expectations.
"""

import os
import json
import tempfile

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


def _write_report(path, region_seed, candidates):
    """Write a minimal p.rank-shaped JSON report with given candidates."""
    rep = {
        "schema":              "p.rank/v1-test",
        "threshold_value":     0.5,
        "n_candidates_found":  len(candidates),
        "candidates":          candidates,
    }
    with open(path, "w") as f:
        json.dump(rep, f)


class TestRankCross(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="prankcross_")
        cls.rep_a = os.path.join(cls.tmp, "region_a_report.json")
        cls.rep_b = os.path.join(cls.tmp, "region_b_report.json")
        # Synthetic data designed so the two parameterised tests below
        # have unambiguous expected winners. Areas span ~7×, small enough
        # that the suit-dominant defaults (suit:0.6, area:0.25, borda:0.15)
        # pick the highest-suit patch (A1), and large enough that the
        # area-dominant weights flip the winner to the largest patch (B2).
        _write_report(cls.rep_a, "A", [
            {"rank": 1, "suit_mean": 0.92, "suit_std": 0.01,
             "area_km2": 1.5, "rank1_probability": 0.80},
            {"rank": 2, "suit_mean": 0.84, "suit_std": 0.02,
             "area_km2": 5.0, "rank1_probability": 0.20},
        ])
        _write_report(cls.rep_b, "B", [
            {"rank": 1, "suit_mean": 0.88, "suit_std": 0.02,
             "area_km2": 3.0,  "rank1_probability": 0.70},
            {"rank": 2, "suit_mean": 0.81, "suit_std": 0.03,
             "area_km2": 10.0, "rank1_probability": 0.30},
        ])

    @classmethod
    def tearDownClass(cls):
        import shutil
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_default_weights_pick_suit_first(self):
        """Default weights are suit-dominant (0.6), so region A's
        top-suit patch should win cross_rank=1."""
        out = os.path.join(self.tmp, "out_default.json")
        self.assertModule("p.rank.cross",
                          reports=f"{self.rep_a},{self.rep_b}",
                          output=out)
        self.assertFileExists(out)
        with open(out) as f:
            rep = json.load(f)
        self.assertEqual(rep["schema"], "p.rank.cross/v1")
        self.assertEqual(rep["n_regions"], 2)
        self.assertEqual(rep["n_candidates"], 4)
        self.assertGreaterEqual(rep["n_returned"], 4)
        top = rep["candidates"][0]
        self.assertEqual(top["cross_rank"], 1)
        self.assertEqual(top["region"], "region_a")
        self.assertAlmostEqual(top["suit_mean"], 0.92, places=4)
        # composite scores must be monotonically non-increasing
        scores = [c["composite_score"] for c in rep["candidates"]]
        self.assertEqual(scores, sorted(scores, reverse=True))

    def test_area_dominant_weights_flip_winner(self):
        """With area weight raised to 0.9 and linear transform, the
        largest patch (region B, 10 km²) should overtake region A's
        small high-suit one."""
        out = os.path.join(self.tmp, "out_area.json")
        self.assertModule("p.rank.cross",
                          reports=f"{self.rep_a},{self.rep_b}",
                          weights="suit:0.05,area:0.9,borda:0.05",
                          area_transform="linear",
                          output=out)
        with open(out) as f:
            rep = json.load(f)
        top = rep["candidates"][0]
        self.assertEqual(top["region"], "region_b")
        self.assertAlmostEqual(top["area_km2"], 10.0)

    def test_region_ids_override(self):
        out = os.path.join(self.tmp, "out_ids.json")
        self.assertModule("p.rank.cross",
                          reports=f"{self.rep_a},{self.rep_b}",
                          region_ids="alpha,bravo", output=out)
        with open(out) as f:
            rep = json.load(f)
        regs = {r["region_id"] for r in rep["regions"]}
        self.assertEqual(regs, {"alpha", "bravo"})
        self.assertIn(rep["candidates"][0]["region"], regs)

    def test_n_top_truncation(self):
        out = os.path.join(self.tmp, "out_ntop.json")
        self.assertModule("p.rank.cross",
                          reports=f"{self.rep_a},{self.rep_b}",
                          n_top=2, output=out)
        with open(out) as f:
            rep = json.load(f)
        self.assertEqual(len(rep["candidates"]), 2)
        self.assertEqual([c["cross_rank"] for c in rep["candidates"]], [1, 2])

    def test_components_recorded(self):
        """Every output candidate must carry the three normalised
        component scores so the composite is auditable."""
        out = os.path.join(self.tmp, "out_components.json")
        self.assertModule("p.rank.cross",
                          reports=f"{self.rep_a},{self.rep_b}",
                          output=out)
        with open(out) as f:
            rep = json.load(f)
        for c in rep["candidates"]:
            self.assertIn("components", c)
            for k in ("suit_norm", "area_norm", "borda_norm"):
                self.assertIn(k, c["components"])
                self.assertGreaterEqual(c["components"][k], 0.0)
                self.assertLessEqual(c["components"][k], 1.0)


if __name__ == "__main__":
    test()
