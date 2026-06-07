"""
Testsuite for p.terrain.slope.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.terrain.slope/testsuite/test_p_terrain_slope.py -v

Builds a small synthetic metric DEM and checks the multi-scale slope and
hazard-mask outputs exist with sane value ranges.
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestSlope(TestCase):

    dem = "test_slope_dem"
    prefix = "tslope"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=30)
        # Relief in metres with both row and column structure.
        gs.mapcalc(f"{cls.dem} = 2000.0 + 8.0*sin(row()*18) "
                   f"+ 4.0*col() + 12.0*sin(col()*9)",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        names = [cls.dem, f"{cls.prefix}_30m", f"{cls.prefix}_mask_30m",
                 f"{cls.prefix}_safe_mask"]
        gs.run_command("g.remove", type="raster", name=",".join(names),
                       flags="f", quiet=True)
        cls.del_temp_region()

    def test_slope_outputs(self):
        self.assertModule("p.terrain.slope", dem=self.dem,
                          scales="30", thresholds="10",
                          prefix=self.prefix, overwrite=True)
        self.assertRasterExists(f"{self.prefix}_30m")
        self.assertRasterExists(f"{self.prefix}_mask_30m")
        self.assertRasterExists(f"{self.prefix}_safe_mask")
        # Slope is an angle in [0, 90] degrees.
        self.assertRasterMinMax(f"{self.prefix}_30m", 0, 90)
        # Masks are binary.
        self.assertRasterMinMax(f"{self.prefix}_mask_30m", 0, 1)
        self.assertRasterMinMax(f"{self.prefix}_safe_mask", 0, 1)


if __name__ == "__main__":
    test()
