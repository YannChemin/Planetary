"""
Testsuite for p.in.ancillary.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.ancillary/testsuite/test_p_in_ancillary.py -v

Round-trips a synthetic ancillary layer (GeoTIFF) and checks import, the
optional [0,1] normalization (-n), and scale/offset handling.
"""

import os
import tempfile

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestInAncillary(TestCase):

    src = "test_anc_src"
    out_plain = "test_anc_out"
    out_norm = "test_anc_norm"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=900, s=0, e=900, w=0, res=30)
        # An ancillary field with a clear min/max (e.g. a WEH proxy 0..200).
        gs.mapcalc(f"{cls.src} = 100.0 + 100.0*sin(row()*12)", overwrite=True)
        cls.tmp = tempfile.mkdtemp(prefix="pinanc_")
        cls.tif = os.path.join(cls.tmp, "anc.tif")
        gs.run_command("r.out.gdal", input=cls.src, output=cls.tif,
                       format="GTiff", type="Float32", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        import shutil
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.src},{cls.out_plain},{cls.out_norm}",
                       flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def test_import_plain(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        self.assertRasterExists(self.out_plain)

    def test_import_normalized(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_norm, type="custom",
                          flags="n", overwrite=True)
        self.assertRasterExists(self.out_norm)
        # -n normalizes to [0, 1].
        self.assertRasterMinMax(self.out_norm, 0, 1)


if __name__ == "__main__":
    test()
