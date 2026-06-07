"""
Testsuite for p.visibility.los.

Run with (in a planetary, projected Location):
    grass <lunar-mapset> --exec python -m pytest \
        p.visibility.los/testsuite/test_p_visibility_los.py -v

Needs a planetary CRS, a body descriptor and r.horizon. Skips in a plain XY
Location.
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


class TestVisibilityLos(TestCase):

    dem = "test_los_dem"
    prefix = "tlos"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=60)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 25.0*sin(row()*10) + 18.0*sin(col()*10)",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.dem},{cls.prefix}_horizon_max",
                       flags="f", quiet=True)
        cls.del_temp_region()

    def test_horizon_max(self):
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        self.assertModule("p.visibility.los", dem=self.dem, body=BODY,
                          directions=8, prefix=self.prefix, overwrite=True)
        self.assertRasterExists(f"{self.prefix}_horizon_max")
        # Horizon angle is bounded to +-90 degrees.
        self.assertRasterMinMax(f"{self.prefix}_horizon_max", -90, 90)


if __name__ == "__main__":
    test()
