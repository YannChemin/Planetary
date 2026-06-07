"""
Testsuite for p.illumination.shadow.

Run with (in a planetary, projected Location):
    grass <lunar-mapset> --exec python -m pytest \
        p.illumination.shadow/testsuite/test_p_illumination_shadow.py -v

Needs a planetary CRS, a body descriptor, and a shadow-mask module. To avoid a
dependency on the compiled p.sunmask, the test calls GRASS core r.sunmask. In a
plain XY Location the tests skip.
"""

import os

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BODY = os.path.join(_ROOT, "bodies", "moon.json")


def _is_planetary():
    try:
        info = gs.parse_command("g.proj", flags="g")
    except Exception:
        return False
    proj = (info.get("proj") or info.get("name") or "").lower()
    return proj not in ("", "xy", "local")


class TestIlluminationShadow(TestCase):

    dem = "test_shadow_dem"
    prefix = "tshadow"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=60)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 20.0*sin(row()*12) + 15.0*sin(col()*12)",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        outs = [f"{cls.prefix}_{s}" for s in
                ("frequency", "mask", "variability", "extreme_incidence")]
        gs.run_command("g.remove", type="raster",
                       name=",".join([cls.dem] + outs), flags="f", quiet=True)
        cls.del_temp_region()

    def test_shadow_outputs(self):
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        self.assertModule("p.illumination.shadow", dem=self.dem, body=BODY,
                          nsteps=6, ephemeris="analytic",
                          sunmask_module="r.sunmask",
                          prefix=self.prefix, overwrite=True)
        self.assertRasterExists(f"{self.prefix}_frequency")
        self.assertRasterExists(f"{self.prefix}_mask")
        # Shadow frequency in [0, 1]; hazard mask binary.
        self.assertRasterMinMax(f"{self.prefix}_frequency", 0, 1)
        self.assertRasterMinMax(f"{self.prefix}_mask", 0, 1)


if __name__ == "__main__":
    test()
