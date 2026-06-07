"""
Testsuite for p.terrain.roughness.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.terrain.roughness/testsuite/test_p_terrain_roughness.py -v
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestRoughness(TestCase):

    dem = "test_rough_dem"
    prefix = "trough"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1500, s=0, e=1500, w=0, res=30)
        gs.mapcalc(f"{cls.dem} = 1000.0 + 6.0*sin(row()*22) "
                   f"+ 6.0*sin(col()*22) + 0.5*row()",
                   overwrite=True)

    @classmethod
    def tearDownClass(cls):
        outs = [f"{cls.prefix}_{s}" for s in ("rms", "cv", "moransI", "mask")]
        gs.run_command("g.remove", type="raster",
                       name=",".join([cls.dem] + outs), flags="f", quiet=True)
        cls.del_temp_region()

    def test_roughness_outputs(self):
        self.assertModule("p.terrain.roughness", dem=self.dem,
                          prefix=self.prefix, overwrite=True)
        for s in ("rms", "cv", "moransI", "mask"):
            self.assertRasterExists(f"{self.prefix}_{s}")
        # RMS height is non-negative; mask is binary.
        info = gs.raster_info(f"{self.prefix}_rms")
        self.assertGreaterEqual(info["min"], 0.0)
        self.assertRasterMinMax(f"{self.prefix}_mask", 0, 1)


if __name__ == "__main__":
    test()
