"""
Testsuite for p.visibility.orbiter.

Run with (in a planetary, projected Location):
    grass <lunar-mapset> --exec python -m pytest \
        p.visibility.orbiter/testsuite/test_p_visibility_orbiter.py -v

Needs a planetary CRS, a body descriptor and r.horizon. Skips in a plain XY
Location. norbits/steps are kept small for speed.
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


class TestVisibilityOrbiter(TestCase):

    dem = "test_orb_dem"
    prefix = "torb"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=60)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 10.0*sin(row()*12) + 8.0*sin(col()*12)",
                   overwrite=True)
        # The module declares no output= option, so g.parser doesn't add
        # the --overwrite flag and pygrass refuses overwrite= kwargs.
        # Pre-clean stale outputs so the tests start from scratch.
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{cls.prefix}_a_contact_fraction,"
                            f"{cls.prefix}_b_contact_fraction")

    @classmethod
    def tearDownClass(cls):
        names = ",".join([cls.dem,
                          f"{cls.prefix}_a_contact_fraction",
                          f"{cls.prefix}_b_contact_fraction"])
        gs.run_command("g.remove", type="raster", name=names,
                       flags="f", quiet=True)
        cls.del_temp_region()

    def test_contact_fraction(self):
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        pfx = f"{self.prefix}_a"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_contact_fraction")
        self.assertModule("p.visibility.orbiter", dem=self.dem, body=BODY,
                          altitude_km=100, inclination=90,
                          norbits=2, steps_per_orbit=12, prefix=pfx)
        self.assertRasterExists(f"{pfx}_contact_fraction")
        # Contact fraction in [0, 1].
        self.assertRasterMinMax(f"{pfx}_contact_fraction", 0, 1)

    def test_contact_fraction_is_float_not_integer_divided(self):
        """Regression: earlier versions stored *_contact_fraction as CELL
        (integer accumulator / integer n_used), truncating every pixel to 0.
        The output must be DCELL/FCELL and produce some non-zero pixels."""
        if not _is_planetary():
            self.skipTest("requires a planetary (non-XY) Location")
        if not os.path.isfile(BODY):
            self.skipTest(f"body descriptor not found: {BODY}")
        pfx = f"{self.prefix}_b"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_contact_fraction")
        self.assertModule("p.visibility.orbiter", dem=self.dem, body=BODY,
                          altitude_km=100, inclination=90,
                          norbits=3, steps_per_orbit=24, prefix=pfx)
        info = gs.parse_command("r.info", map=f"{pfx}_contact_fraction",
                                flags="g")
        self.assertIn(info.get("datatype"), ("DCELL", "FCELL"),
            "contact_fraction must be float; CELL means accum/n_used was "
            "integer-divided.")
        stats = gs.parse_command("r.univar", map=f"{pfx}_contact_fraction",
                                 flags="g")
        self.assertGreater(float(stats["max"]), 0.0,
            "Contact fraction max==0 — the per-step integer-division "
            "regression is back.")


if __name__ == "__main__":
    test()
