"""
Testsuite for p.in.pds4.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.pds4/testsuite/test_pinpds4.py -v

Interface tests run unconditionally.  The synthetic-import tests create a
minimal PDS4 XML label + binary data file, import with p.in.pds4, and verify
the raster output and planetary.json sidecar.  Skipped if p.in.pds4 is not
installed.
"""

import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
from grass.gunittest.main import test

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from p_meta import METADATA_FILENAME, SCHEMA_VERSION


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _cell_misc_path(mapname: str) -> Path:
    env = gs.gisenv()
    return (
        Path(env["GISDBASE"]) / env["LOCATION_NAME"] / env["MAPSET"]
        / "cell_misc" / mapname / METADATA_FILENAME
    )


def _make_synthetic_pds4(tmpdir: str) -> str:
    """Create a minimal PDS4 XML label + raw binary (4×4 IEEE754 MSB singles).

    Returns the path to the XML label file.  The data file sits alongside it.
    Pixel values are 0.0 … 15.0 in row-major BSQ order.
    """
    data_fname = "synthetic_4x4.dat"
    xml_fname = "synthetic_4x4.xml"
    data_path = os.path.join(tmpdir, data_fname)
    xml_path = os.path.join(tmpdir, xml_fname)

    # 4×4 IEEE754 big-endian singles
    pixels = struct.pack(">16f", *[float(i) for i in range(16)])
    with open(data_path, "wb") as fh:
        fh.write(pixels)

    # Minimal PDS4 label.  Uses local-name() fallback XPath paths already
    # supported by p.in.pds4, so namespace declaration is not required.
    xml = f"""\
<?xml version="1.0" encoding="UTF-8"?>
<Product_Observational xmlns="http://pds.nasa.gov/pds4/pds/v1">
  <Identification_Area>
    <logical_identifier>urn:test:pds4:synthetic::1.0</logical_identifier>
    <version_id>1.0</version_id>
    <title>Synthetic 4x4 test image</title>
    <product_class>Product_Observational</product_class>
  </Identification_Area>
  <Observation_Area>
    <Target_Identification>
      <name>TEST_BODY</name>
    </Target_Identification>
    <Instrument>
      <name>TEST_CAM</name>
    </Instrument>
    <Mission_Information>
      <mission_name>TEST_MISSION</mission_name>
    </Mission_Information>
    <Start_Date_Time>2000-01-01T00:00:00Z</Start_Date_Time>
  </Observation_Area>
  <File_Area_Observational>
    <File>
      <file_name>{data_fname}</file_name>
    </File>
    <Array_2D_Image>
      <axes>2</axes>
      <axis_index_order>Last Index Fastest</axis_index_order>
      <Element_Array>
        <data_type>IEEE754MSBSingle</data_type>
      </Element_Array>
      <Axis_Array>
        <axis_name>Line</axis_name>
        <elements>4</elements>
        <sequence_number>1</sequence_number>
      </Axis_Array>
      <Axis_Array>
        <axis_name>Sample</axis_name>
        <elements>4</elements>
        <sequence_number>2</sequence_number>
      </Axis_Array>
      <lines>4</lines>
      <samples>4</samples>
      <offset unit="byte">0</offset>
    </Array_2D_Image>
  </File_Area_Observational>
</Product_Observational>
"""
    with open(xml_path, "w") as fh:
        fh.write(xml)
    return xml_path


# ---------------------------------------------------------------------------
# Interface tests
# ---------------------------------------------------------------------------

class TestPinPds4Interface(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()

    def test_help(self):
        module = SimpleModule("p.in.pds4", flags="h")
        self.assertModule(module)

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.pds4"),
                             "p.in.pds4 not found in PATH")


# ---------------------------------------------------------------------------
# Synthetic PDS4 import + planetary.json tests
# ---------------------------------------------------------------------------

@unittest.skipUnless(shutil.which("p.in.pds4"),
                     "p.in.pds4 not installed — skipping synthetic import tests")
class TestPinPds4Synthetic(TestCase):
    """Import a self-contained synthetic PDS4 product and verify outputs."""

    out = "test_pds4_synth"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.tmp = tempfile.mkdtemp(prefix="pinpds4_")
        cls.xml_file = _make_synthetic_pds4(cls.tmp)
        cls.runModule("g.region", rows=4, cols=4,
                      n=4, s=0, e=4, w=0, res=1)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", name=cls.out,
                       flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def test_import_creates_raster(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        self.assertRasterExists(self.out)

    def test_import_pixel_range(self):
        """Pixels 0.0..15.0 must survive the import."""
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        info = gs.raster_info(self.out)
        self.assertAlmostEqual(info["min"], 0.0, delta=0.5)
        self.assertAlmostEqual(info["max"], 15.0, delta=0.5)

    def test_planetary_json_created(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_schema_version(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    def test_planetary_json_data_type_image(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["data_type"], "image")

    def test_planetary_json_sensor_from_label(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertIsNotNone(data.get("sensor"),
                             "sensor should be populated from Instrument/name")

    def test_planetary_json_body_from_label(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("body", planetary)

    def test_planetary_json_mission_from_label(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("mission", planetary)

    def test_planetary_json_pds_product_id(self):
        """logical_identifier from the label must appear as pds_product_id."""
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("pds_product_id", planetary)
        self.assertIn("urn:test", planetary["pds_product_id"])

    def test_planetary_json_source_file(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("source_file", planetary)
        self.assertIn("synthetic_4x4.xml", planetary["source_file"])

    def test_planetary_json_radiometric(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data.get("radiometric_quantity"), "raw_dn")
        self.assertEqual(data.get("radiometric_units"), "DN")

    def test_planetary_json_acquisition_datetime(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertIsNotNone(data.get("acquisition_datetime"))
        self.assertIn("2000-01-01", data["acquisition_datetime"])

    def test_planetary_json_first_write_wins(self):
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        mtime1 = path.stat().st_mtime_ns
        self.assertModule("p.in.pds4", input=self.xml_file,
                          output=self.out, overwrite=True)
        mtime2 = path.stat().st_mtime_ns
        self.assertEqual(mtime1, mtime2,
                         "planetary.json must not be overwritten on reimport")


if __name__ == "__main__":
    test()
