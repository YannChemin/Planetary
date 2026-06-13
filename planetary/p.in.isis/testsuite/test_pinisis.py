"""
Testsuite for p.in.isis.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.isis/testsuite/test_pinisis.py -v

Interface tests run unconditionally.  The synthetic-import test creates a
minimal ISIS3 cube file using the GDAL VRT driver (which GDAL can convert to
ISIS3 format), imports it with p.in.isis, and verifies the raster output and
planetary.json sidecar.

ISIS3 .cub creation requires either:
  - GDAL with ISIS3 write support (GDAL >= 3.0), detected via
    ``gdal-config --formats | grep -i isis3``, OR
  - $ISISROOT being set (ISIS3 installed)

If neither is available, the synthetic tests are skipped gracefully.
"""

import json
import os
import shutil
import subprocess
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
# Feature detection
# ---------------------------------------------------------------------------

def _gdal_has_isis3_write() -> bool:
    """Return True if gdal_translate can write ISIS3 format."""
    try:
        out = subprocess.check_output(
            ["gdal-config", "--formats"], stderr=subprocess.DEVNULL,
            text=True,
        )
        return "isis3" in out.lower()
    except Exception:
        return False


def _gdal_has_isis3_read() -> bool:
    """Return True if GDAL can at least read ISIS3 (needed for r.in.gdal)."""
    try:
        out = subprocess.check_output(
            ["gdalinfo", "--formats"], stderr=subprocess.DEVNULL,
            text=True,
        )
        return "isis3" in out.lower()
    except Exception:
        return False


HAS_ISIS3_WRITE = _gdal_has_isis3_write()
HAS_ISIS3_READ  = _gdal_has_isis3_read()
HAS_ISIS_ENV    = bool(os.environ.get("ISISROOT", ""))


def _cell_misc_path(mapname: str) -> Path:
    env = gs.gisenv()
    return (
        Path(env["GISDBASE"]) / env["LOCATION_NAME"] / env["MAPSET"]
        / "cell_misc" / mapname / METADATA_FILENAME
    )


# ---------------------------------------------------------------------------
# Synthetic ISIS3 cube creation
# ---------------------------------------------------------------------------

def _make_synthetic_isis3(tmpdir: str) -> str:
    """Create a minimal ISIS3 .cub (4×4, uint8) via gdal_translate.

    Writes a GeoTIFF first, then converts to ISIS3 via gdal_translate.
    Returns the path to the .cub file, or None if conversion fails.
    """
    import struct
    tif_path = os.path.join(tmpdir, "synthetic.tif")
    cub_path = os.path.join(tmpdir, "synthetic.cub")

    # 4×4 uint8 raw bytes in a minimal TIFF using gdal_create
    rc = subprocess.run(
        [
            "gdal_create",
            "-ot", "Byte",
            "-outsize", "4", "4",
            "-bands", "1",
            tif_path,
        ],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode
    if rc != 0:
        return None

    # Fill with 0..15
    try:
        from osgeo import gdal, gdal_array
        import numpy as np
        ds = gdal.Open(tif_path, gdal.GA_Update)
        if ds is None:
            return None
        arr = np.arange(16, dtype=np.uint8).reshape(4, 4)
        ds.GetRasterBand(1).WriteArray(arr)
        ds = None
    except Exception:
        return None

    rc = subprocess.run(
        ["gdal_translate", "-of", "ISIS3", tif_path, cub_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode
    if rc != 0 or not os.path.exists(cub_path):
        return None
    return cub_path


# ---------------------------------------------------------------------------
# Interface tests
# ---------------------------------------------------------------------------

class TestPinIsisInterface(TestCase):

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()

    def test_help(self):
        module = SimpleModule("p.in.isis", flags="h")
        self.assertModule(module)

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.isis"),
                             "p.in.isis not found in PATH")

    @unittest.skipUnless(shutil.which("isis2std"),
                         "ISIS3 isis2std not available — skipping cross-validation")
    def test_isis3_equivalence(self):
        self.skipTest("Full ISIS3 cross-validation requires reference data fixture")


# ---------------------------------------------------------------------------
# Synthetic ISIS3 import + planetary.json tests
# ---------------------------------------------------------------------------

@unittest.skipUnless(
    shutil.which("p.in.isis") and HAS_ISIS3_WRITE and HAS_ISIS3_READ,
    "p.in.isis not installed or GDAL ISIS3 write support unavailable"
)
class TestPinIsisSynthetic(TestCase):
    """Import a gdal_translate-generated ISIS3 cube and verify outputs."""

    out = "test_isis_synth"
    _cub = None

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.tmp = tempfile.mkdtemp(prefix="pinisis_")
        cls._cub = _make_synthetic_isis3(cls.tmp)
        if cls._cub is None:
            return  # individual tests will skip via _require_cub()
        cls.runModule("g.region", rows=4, cols=4,
                      n=4, s=0, e=4, w=0, res=1)

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", name=cls.out,
                       flags="f", quiet=True)
        shutil.rmtree(cls.tmp, ignore_errors=True)
        cls.del_temp_region()

    def _require_cub(self):
        if not self._cub:
            self.skipTest("Synthetic ISIS3 cube could not be created")

    def test_import_creates_raster(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        self.assertRasterExists(self.out)

    def test_planetary_json_created(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_schema_version(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    def test_planetary_json_data_type_image(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["data_type"], "image")

    def test_planetary_json_radiometric(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data.get("radiometric_quantity"), "raw_dn")
        self.assertEqual(data.get("radiometric_units"), "DN")

    def test_planetary_json_source_file(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("source_file", planetary)
        self.assertIn("synthetic.cub", planetary["source_file"])

    def test_planetary_json_first_write_wins(self):
        self._require_cub()
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        path = _cell_misc_path(self.out)
        mtime1 = path.stat().st_mtime_ns
        self.assertModule("p.in.isis", input=self._cub,
                          output=self.out, overwrite=True)
        mtime2 = path.stat().st_mtime_ns
        self.assertEqual(mtime1, mtime2,
                         "planetary.json must not be overwritten on reimport")


if __name__ == "__main__":
    test()
