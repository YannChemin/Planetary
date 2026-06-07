"""
Testsuite for p.sunmask (C + OpenMP/OpenCL shadow caster).

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.sunmask/testsuite/test_p_sunmask.py -v

Casts a shadow mask for a synthetic hill at a low sun altitude and checks the
output is a binary 1=sunlit / 0=shadow raster. Skips if the compiled p.sunmask
binary is not on the GRASS module path.
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

HAS_SUNMASK = bool(gs.find_program("p.sunmask", "--help"))


class TestSunmask(TestCase):

    dem = "test_sun_dem"
    out = "test_sun_mask"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1500, s=0, e=1500, w=0, res=30)
        # A tall central hill (~400 m) on a flat plain — casts a shadow at
        # low sun elevation.
        gs.mapcalc(
            f"{cls.dem} = 1000.0 + 400.0*exp(-((row()-25.0)^2 "
            f"+ (col()-25.0)^2)/40.0)", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.dem},{cls.out}", flags="f", quiet=True)
        cls.del_temp_region()

    @test.skipIf(not HAS_SUNMASK, "compiled p.sunmask not on the module path")
    def test_shadow_mask_is_binary(self):
        self.assertModule("p.sunmask", elevation=self.dem, output=self.out,
                          azimuth=135, altitude=15, overwrite=True)
        self.assertRasterExists(self.out)
        # 1 = sunlit, 0 = shadow.
        self.assertRasterMinMax(self.out, 0, 1)
        # Most of an open plain at 15 deg sun should be lit.
        info = gs.raster_info(self.out)
        self.assertEqual(info["max"], 1)


if __name__ == "__main__":
    test()
