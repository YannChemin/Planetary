"""
Testsuite for p.in.dem.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.dem/testsuite/test_p_in_dem.py -v

Round-trips a synthetic DEM: a GRASS raster is exported to a GeoTIFF, then
re-imported with p.in.dem and checked for existence, matching value range, and
a valid planetary.json metadata sidecar.
"""

import json
import os
import sys
import tempfile
from pathlib import Path

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

# Allow p_meta to be imported for the metadata helper.
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from p_meta import METADATA_FILENAME, SCHEMA_VERSION


def _cell_misc_path(mapname: str) -> Path:
    env = gs.gisenv()
    return (
        Path(env["GISDBASE"]) / env["LOCATION_NAME"] / env["MAPSET"]
        / "cell_misc" / mapname / METADATA_FILENAME
    )


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

    # -- raster import -------------------------------------------------------

    def test_import_geotiff(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        self.assertRasterExists(self.out)
        src_info = gs.raster_info(self.src)
        out_info = gs.raster_info(self.out)
        self.assertAlmostEqual(out_info["min"], src_info["min"], delta=5.0)
        self.assertAlmostEqual(out_info["max"], src_info["max"], delta=5.0)

    # -- planetary.json ------------------------------------------------------

    def test_planetary_json_created(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        path = _cell_misc_path(self.out)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_schema_version(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    def test_planetary_json_data_type_dem(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data["data_type"], "dem")

    def test_planetary_json_radiometric(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        self.assertEqual(data.get("radiometric_quantity"), "elevation")
        self.assertEqual(data.get("radiometric_units"), "m")

    def test_planetary_json_source_file(self):
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("source_file", planetary)

    def test_planetary_json_first_write_wins(self):
        """Importing the same output twice must not overwrite planetary.json."""
        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        path = _cell_misc_path(self.out)
        mtime1 = path.stat().st_mtime_ns

        self.assertModule("p.in.dem", input=self.tif, output=self.out,
                          flags="r", overwrite=True)
        mtime2 = path.stat().st_mtime_ns
        self.assertEqual(mtime1, mtime2,
                         "planetary.json must not be overwritten on second import")


if __name__ == "__main__":
    test()
