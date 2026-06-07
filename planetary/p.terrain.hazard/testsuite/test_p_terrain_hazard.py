"""
Testsuite for p.terrain.hazard.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.terrain.hazard/testsuite/test_p_terrain_hazard.py -v

The module derives its own slope/roughness from the DEM when they are not
supplied, so only a DEM is needed.
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestHazard(TestCase):

    dem = "test_haz_dem"
    prefix = "thaz"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1500, s=0, e=1500, w=0, res=30)
        gs.mapcalc(f"{cls.dem} = 1500.0 + 10.0*sin(row()*15) "
                   f"+ 7.0*col() + 15.0*sin(col()*11)",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        outs = [f"{cls.prefix}_{s}" for s in
                ("composite", "mask", "slope", "roughness", "relief", "curvature")]
        gs.run_command("g.remove", type="raster",
                       name=",".join([cls.dem] + outs), flags="f", quiet=True)
        cls.del_temp_region()

    def test_hazard_outputs(self):
        self.assertModule("p.terrain.hazard", dem=self.dem,
                          slope_max=15, roughness_max=1.0,
                          prefix=self.prefix, overwrite=True)
        self.assertRasterExists(f"{self.prefix}_composite")
        self.assertRasterExists(f"{self.prefix}_mask")
        # Composite hazard is a normalized score in [0, 1]; mask is binary.
        self.assertRasterMinMax(f"{self.prefix}_composite", 0, 1)
        self.assertRasterMinMax(f"{self.prefix}_mask", 0, 1)


if __name__ == "__main__":
    test()
