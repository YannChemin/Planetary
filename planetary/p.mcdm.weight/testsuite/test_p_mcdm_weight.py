"""
Testsuite for p.mcdm.weight (AHP weight elicitation).

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.mcdm.weight/testsuite/test_p_mcdm_weight.py -v

Feeds a perfectly-consistent 3x3 pairwise matrix and checks the resulting
weights are normalized and the consistency ratio is ~0.
"""

import os
import json
import tempfile

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestMcdmWeight(TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="mcdmw_")
        # Consistent matrix: a=2b, a=4c, b=2c  -> weights 4:2:1.
        self.pw = os.path.join(self.tmp, "pw.csv")
        with open(self.pw, "w") as f:
            f.write("1,2,4\n0.5,1,2\n0.25,0.5,1\n")
        self.out = os.path.join(self.tmp, "weights.json")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_weights_and_consistency(self):
        self.assertModule("p.mcdm.weight", pairwise=self.pw,
                          criteria="slope,roughness,illumination",
                          output=self.out, overwrite=True)
        self.assertFileExists(self.out)
        with open(self.out) as f:
            res = json.load(f)
        # Extract the weights mapping regardless of exact key name.
        weights = res.get("weights") or res.get("weight") or {}
        self.assertEqual(len(weights), 3)
        total = sum(float(v) for v in weights.values())
        self.assertAlmostEqual(total, 1.0, delta=1e-3)
        # A perfectly consistent matrix has CR ~ 0.
        cr = float(res.get("consistency_ratio", res.get("CR", 0.0)))
        self.assertLess(cr, 0.05)
        # Largest weight should be the first criterion (slope, ratio 4:2:1).
        self.assertAlmostEqual(weights["slope"], 4.0 / 7.0, delta=0.02)


if __name__ == "__main__":
    test()
