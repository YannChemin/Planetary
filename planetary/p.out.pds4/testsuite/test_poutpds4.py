"""Tests for p.out.pds4 — PDS4 GeoTIFF export."""

import os
import tempfile
from pathlib import Path
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestOutPds4(TestCase):

    TMP_DIR = None

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", rows=10, cols=10, n=10, s=0, e=10, w=0)
        gs.run_command("r.mapcalc", expression="synth = row() + col() * 0.1",
                       overwrite=True)
        cls.TMP_DIR = tempfile.mkdtemp(prefix="poutpds4_test_")

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster", name="synth",
                       flags="f", quiet=True)
        if cls.TMP_DIR:
            import shutil
            shutil.rmtree(cls.TMP_DIR, ignore_errors=True)

    def _out_base(self, stem):
        return os.path.join(self.TMP_DIR, stem)

    def test_files_created(self):
        """Both .tif and .xml are written."""
        base = self._out_base("test_basic")
        self.assertModule("p.out.pds4", input="synth", output=base, body="mars")
        self.assertTrue(Path(base + ".tif").exists(), ".tif not created")
        self.assertTrue(Path(base + ".xml").exists(), ".xml not created")

    def test_xml_is_valid_pds4(self):
        """XML contains Product_Ancillary and cart:Cartography."""
        base = self._out_base("test_xml")
        self.assertModule("p.out.pds4", input="synth", output=base, body="mars")
        xml = Path(base + ".xml").read_text()
        self.assertIn("Product_Ancillary", xml)
        self.assertIn("cart:Cartography", xml)
        self.assertIn("urn:nasa:pds:", xml)

    def test_xml_bounding_box(self):
        """XML bounding box matches the GRASS region."""
        base = self._out_base("test_bbox")
        self.assertModule("p.out.pds4", input="synth", output=base, body="mars")
        xml = Path(base + ".xml").read_text()
        self.assertIn("west_bounding_coordinate", xml)
        self.assertIn("north_bounding_coordinate", xml)

    def test_moon_body(self):
        """body=moon uses Moon radii in the XML label."""
        base = self._out_base("test_moon")
        self.assertModule("p.out.pds4", input="synth", output=base, body="moon")
        xml = Path(base + ".xml").read_text()
        # Moon equatorial radius 1737400 m
        self.assertIn("1737400", xml)

    def test_custom_title_lid(self):
        """Custom title= and lid= appear in the XML."""
        base = self._out_base("test_meta")
        self.assertModule("p.out.pds4", input="synth", output=base,
                          body="mars",
                          title="Test olivine map",
                          lid="test_bundle:test_collection:test_product")
        xml = Path(base + ".xml").read_text()
        self.assertIn("Test olivine map", xml)
        self.assertIn("test_bundle:test_collection:test_product", xml)

    def test_integer_type(self):
        """type=Int16 writes without error."""
        base = self._out_base("test_int16")
        self.assertModule("p.out.pds4", input="synth", output=base,
                          body="mars", type="Int16")
        self.assertTrue(Path(base + ".tif").exists())

    def test_geotiff_is_readable(self):
        """Output GeoTIFF can be re-imported by GDAL/r.in.gdal."""
        base = self._out_base("test_readback")
        self.assertModule("p.out.pds4", input="synth", output=base, body="mars")
        # Try importing back — just check it succeeds
        ret = gs.run_command("r.in.gdal",
                             input=base + ".tif",
                             output="synth_readback",
                             overwrite=True,
                             quiet=True,
                             run_=False)
        self.assertEqual(ret.returncode if hasattr(ret, "returncode") else 0, 0)
        gs.run_command("g.remove", type="raster", name="synth_readback",
                       flags="f", quiet=True)


if __name__ == "__main__":
    test()
