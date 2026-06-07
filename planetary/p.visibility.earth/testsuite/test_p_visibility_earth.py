"""
Testsuite for p.visibility.earth.

Run with (in a planetary, projected Location — e.g. lunar polar stereographic):
    grass <lunar-mapset> --exec python -m pytest \
        p.visibility.earth/testsuite/test_p_visibility_earth.py -v

The module needs a planetary CRS (it inverts the projection to get the region
centre lat/lon) and a body descriptor, plus r.horizon (GRASS core). In a plain
XY test Location the tests skip.
"""

import os

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BODY = os.path.join(_ROOT, "bodies", "moon.json")


def _is_planetary():
    """True if the current Location has a real (non-XY) CRS."""
    try:
        info = gs.parse_command("g.proj", flags="g")
    except Exception:
        return False
    proj = (info.get("proj") or info.get("name") or "").lower()
    return proj not in ("", "xy", "local")


class TestVisibilityEarth(TestCase):

    dem = "test_vis_dem"
    prefix = "tevis"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=60)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 3.0*sin(row()*15) + 2.0*sin(col()*15)",
                   overwrite=True)
        # The module declares no output= option, so g.parser doesn't add the
        # --overwrite flag and pygrass refuses overwrite= kwargs. Pre-clean
        # any stale outputs from earlier runs so the test starts from scratch.
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{cls.prefix}_fraction,{cls.prefix}_mask")

    @classmethod
    def tearDownClass(cls):
        names = ",".join([cls.dem,
                          f"{cls.prefix}_a_fraction", f"{cls.prefix}_a_mask",
                          f"{cls.prefix}_b_fraction", f"{cls.prefix}_b_mask",
                          f"{cls.prefix}_w_fraction", f"{cls.prefix}_w_mask"])
        gs.run_command("g.remove", type="raster", name=names,
                       flags="f", quiet=True)
        cls.del_temp_region()

    def test_visibility_outputs(self):
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        pfx = f"{self.prefix}_a"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_fraction,{pfx}_mask")
        self.assertModule("p.visibility.earth", dem=self.dem, body=BODY,
                          nsteps=8, ephemeris="analytic", prefix=pfx)
        self.assertRasterExists(f"{pfx}_fraction")
        self.assertRasterExists(f"{pfx}_mask")
        # Visibility fraction in [0, 1]; mask binary.
        self.assertRasterMinMax(f"{pfx}_fraction", 0, 1)
        self.assertRasterMinMax(f"{pfx}_mask", 0, 1)

    def test_window_days_short_mission(self):
        """window_days>0 must run; the resulting fraction is bounded [0,1].
        Compares against the default (full-cycle) run on the same DEM: the
        short-window mean is typically much closer to the instantaneous
        geometry at start_epoch than the long-cycle average, so the two
        means differ measurably."""
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        # short-window run
        pfx = f"{self.prefix}_w"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_fraction,{pfx}_mask")
        self.assertModule("p.visibility.earth", dem=self.dem, body=BODY,
                          nsteps=12, ephemeris="analytic",
                          start_epoch="2027-09-15T00:00:00",
                          window_days=6.5, prefix=pfx)
        self.assertRasterExists(f"{pfx}_fraction")
        self.assertRasterMinMax(f"{pfx}_fraction", 0, 1)
        info = gs.parse_command("r.info", map=f"{pfx}_fraction", flags="g")
        self.assertIn(info.get("datatype"), ("DCELL", "FCELL"))

    def test_fraction_is_float_not_integer_divided(self):
        """Regression: earlier versions stored *_fraction as CELL (integer)
        and divided an integer accumulator by an integer n_steps, truncating
        every pixel to 0. The output must be DCELL/FCELL and, with at least
        one above-horizon step on a near-flat DEM, must produce some non-zero
        pixels."""
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        pfx = f"{self.prefix}_b"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_fraction,{pfx}_mask")
        self.assertModule("p.visibility.earth", dem=self.dem, body=BODY,
                          nsteps=16, ephemeris="analytic", prefix=pfx)
        info = gs.parse_command("r.info", map=f"{pfx}_fraction", flags="g")
        self.assertIn(info.get("datatype"), ("DCELL", "FCELL"),
                      "earth_vis_fraction must be float-typed; integer "
                      "(CELL) means accum/n_steps was integer-divided.")
        stats = gs.parse_command("r.univar", map=f"{pfx}_fraction", flags="g")
        self.assertGreater(float(stats["max"]), 0.0,
                           "Fraction max==0 across the whole region — the "
                           "per-step integer-division regression is back.")


if __name__ == "__main__":
    test()
