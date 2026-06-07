"""
Testsuite for p.in.dem.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.dem/testsuite/test_p_in_dem.py -v

Round-trips a synthetic DEM: a GRASS raster is exported to a GeoTIFF, then
re-imported with p.in.dem and checked for existence and matching value range.
This needs no external data (the p.in.pds testsuite covers the real PDS path).
"""

import os
import tempfile

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestInDem(TestCase):

    src = "test_indem_src"
    out = "test_indem_out"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=1200, s=0, e=1200, w=0, res=30)
        gs.mapcalc(f"{cls.src} = 1000.0 + 5.0*sin(row()*12) + 3.0*col()",
                   overwrite=True)
        cls.tmp = tempfile.mkdtemp(prefix="pindem_")
        cls.tif = os.path.join(cls.tmp, "synthetic_dem.tif")
        gs.run_command("r.out.gdal", input=cls.src, output=cls.tif,
                       format="GTiff", type="Float32", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        import shutil
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.src},{cls.out}", flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def test_import_geotiff(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        self.assertRasterExists(self.out)
        src_info = gs.raster_info(self.src)
        out_info = gs.raster_info(self.out)
        # Imported range should match the source within resampling tolerance.
        self.assertAlmostEqual(out_info["min"], src_info["min"], delta=5.0)
        self.assertAlmostEqual(out_info["max"], src_info["max"], delta=5.0)


if __name__ == "__main__":
    test()
