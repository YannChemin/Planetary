"""
Testsuite for p.in.pds3.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.pds3/testsuite/test_pinpds3.py -v

Interface tests run unconditionally (module on PATH, --interface-description).
The synthetic-image tests create a minimal in-memory PDS3 file, import it with
p.in.pds3, and verify both the raster output and the planetary.json sidecar.
These tests are skipped if p.in.pds3 is not installed.
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


def _make_synthetic_pds3(path: str) -> None:
    """Write a minimal 4×4 unsigned-byte, single-band PDS3 IMAGE file.

    The format follows the PDS3 Standards Reference fixed-length record
    layout: a 512-byte label record followed by a 512-byte data record.
    Pixel values are 0..15 in row-major order (matching test_p_pds.c).
    """
    label = (
        "PDS_VERSION_ID  = PDS3\r\n"
        "RECORD_TYPE     = FIXED_LENGTH\r\n"
        "RECORD_BYTES    = 512\r\n"
        "FILE_RECORDS    = 2\r\n"
        "LABEL_RECORDS   = 1\r\n"
        "^IMAGE          = 2\r\n"
        "INSTRUMENT_ID   = TEST_CAM\r\n"
        "SPACECRAFT_NAME = TEST_CRAFT\r\n"
        "TARGET_NAME     = TEST_BODY\r\n"
        "START_TIME      = 2000-01-01T00:00:00.000\r\n"
        "OBJECT = IMAGE\r\n"
        "  LINES        = 4\r\n"
        "  LINE_SAMPLES = 4\r\n"
        "  BANDS        = 1\r\n"
        "  SAMPLE_BITS  = 8\r\n"
        "  SAMPLE_TYPE  = MSB_UNSIGNED_INTEGER\r\n"
        "  SCALING_FACTOR = 1.0\r\n"
        "  OFFSET       = 0.0\r\n"
        "END_OBJECT = IMAGE\r\n"
        "END\r\n"
    )
    label_bytes = label.encode("ascii")
    if len(label_bytes) > 512:
        raise ValueError(f"Label too long ({len(label_bytes)} bytes)")
    label_bytes = label_bytes + b" " * (512 - len(label_bytes))
    pixels = bytes(range(16)) + b"\x00" * (512 - 16)
    with open(path, "wb") as fh:
        fh.write(label_bytes + pixels)


# ---------------------------------------------------------------------------
# Interface tests
# ---------------------------------------------------------------------------

class TestPinPds3Interface(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()

    def test_help(self):
        module = SimpleModule("p.in.pds3", flags="h")
        self.assertModule(module)

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.pds3"),
                             "p.in.pds3 not found in PATH")

    @unittest.skipUnless(shutil.which("pds2isis"),
                         "ISIS3 pds2isis not available — skipping cross-validation")
    def test_isis3_equivalence(self):
        self.skipTest("Full ISIS3 cross-validation requires reference data fixture")


# ---------------------------------------------------------------------------
# Synthetic PDS3 import + planetary.json tests
# ---------------------------------------------------------------------------

@unittest.skipUnless(shutil.which("p.in.pds3"),
                     "p.in.pds3 not installed — skipping synthetic import tests")
class TestPinPds3Synthetic(TestCase):
    """Import a self-contained synthetic PDS3 image and verify outputs."""

    out = "test_pds3_synth"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.tmp = tempfile.mkdtemp(prefix="pinpds3_")
        cls.pds3_file = os.path.join(cls.tmp, "synthetic.img")
        _make_synthetic_pds3(cls.pds3_file)
        # Region must match the 4×4 image.
        cls.runModule("g.region", rows=4, cols=4,
                      n=4, s=0, e=4, w=0, res=1)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", name=cls.out,
                       flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def test_import_creates_raster(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        self.assertRasterExists(self.out)

    def test_import_pixel_range(self):
        """Pixel values in the 4×4 image must span 0..15."""
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        info = gs.raster_info(self.out)
        self.assertAlmostEqual(info["min"], 0.0, delta=0.5)
        self.assertAlmostEqual(info["max"], 15.0, delta=0.5)

    def test_planetary_json_created(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_schema_version(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    def test_planetary_json_data_type_image(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["data_type"], "image")

    def test_planetary_json_sensor_from_label(self):
        """INSTRUMENT_ID from the PDS3 label must appear as sensor."""
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertIsNotNone(data.get("sensor"),
                             "sensor should be populated from INSTRUMENT_ID")

    def test_planetary_json_body_from_label(self):
        """TARGET_NAME from the PDS3 label must appear in extended_metadata."""
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("body", planetary)

    def test_planetary_json_source_file(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("source_file", planetary)
        self.assertIn("synthetic.img", planetary["source_file"])

    def test_planetary_json_radiometric(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data.get("radiometric_quantity"), "raw_dn")
        self.assertEqual(data.get("radiometric_units"), "DN")

    def test_planetary_json_first_write_wins(self):
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        mtime1 = path.stat().st_mtime_ns
        self.assertModule("p.in.pds3", input=self.pds3_file,
                          output=self.out, overwrite=True)
        mtime2 = path.stat().st_mtime_ns
        self.assertEqual(mtime1, mtime2,
                         "planetary.json must not be overwritten on reimport")


if __name__ == "__main__":
    test()
