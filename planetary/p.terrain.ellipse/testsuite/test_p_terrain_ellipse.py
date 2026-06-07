"""
Testsuite for p.terrain.ellipse.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.terrain.ellipse/testsuite/test_p_terrain_ellipse.py -v

Scans a small synthetic DEM with a small landing-ellipse footprint and checks
the rating raster and per-metric outputs.
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestEllipse(TestCase):

    dem = "test_ell_dem"
    prefix = "tell"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=3000, s=0, e=3000, w=0, res=30)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 5.0*sin(row()*8) + 4.0*sin(col()*8)",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        outs = [f"{cls.prefix}_{s}" for s in
                ("rating", "slope_mean", "ratio_lo", "ratio_hi", "cv", "moransI")]
        gs.run_command("g.remove", type="raster",
                       name=",".join([cls.dem] + outs), flags="f", quiet=True)
        gs.run_command("g.remove", type="vector",
                       name=f"{cls.prefix}_candidates", flags="f", quiet=True)
        cls.del_temp_region()

    def test_ellipse_rating(self):
        self.assertModule("p.terrain.ellipse", dem=self.dem,
                          ellipse_major=300, ellipse_minor=150, scan_res=30,
                          prefix=self.prefix, overwrite=True)
        self.assertRasterExists(f"{self.prefix}_rating")
        self.assertRasterExists(f"{self.prefix}_slope_mean")
        # slope_mean is a non-negative angle.
        info = gs.raster_info(f"{self.prefix}_slope_mean")
        self.assertGreaterEqual(info["min"], 0.0)


if __name__ == "__main__":
    test()
