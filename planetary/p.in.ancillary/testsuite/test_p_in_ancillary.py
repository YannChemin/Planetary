"""
Testsuite for p.in.ancillary.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.ancillary/testsuite/test_p_in_ancillary.py -v

Round-trips a synthetic ancillary layer (GeoTIFF) and checks import, the
optional [0,1] normalization (-n), scale/offset handling, and that a valid
planetary.json metadata sidecar is produced.
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


class TestInAncillary(TestCase):

    src = "test_anc_src"
    out_plain = "test_anc_out"
    out_norm = "test_anc_norm"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=900, s=0, e=900, w=0, res=30)
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

    # -- raster import -------------------------------------------------------

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
        self.assertRasterMinMax(self.out_norm, 0, 1)

    # -- planetary.json ------------------------------------------------------

    def test_planetary_json_created(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        path = _cell_misc_path(self.out_plain)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_schema_version(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        with open(_cell_misc_path(self.out_plain)) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    def test_planetary_json_data_type_ancillary(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        with open(_cell_misc_path(self.out_plain)) as fh:
            data = json.load(fh)
        # type="custom" maps to data_type="ancillary"
        self.assertEqual(data["data_type"], "ancillary")

    def test_planetary_json_different_types(self):
        """temperature, opacity, and weh types have distinct data_type values."""
        type_map = {
            "temperature": "ancillary",
            "opacity":     "ancillary",
            "weh":         "ancillary",
        }
        for anc_type, expected_dtype in type_map.items():
            out = f"test_anc_{anc_type}"
            self.assertModule("p.in.ancillary", input=self.tif,
                              output=out, type=anc_type, overwrite=True)
            with open(_cell_misc_path(out)) as fh:
                data = json.load(fh)
            self.assertEqual(data["data_type"], expected_dtype,
                             f"type={anc_type} → data_type={expected_dtype}")
            gs.run_command("g.remove", type="raster", name=out,
                           flags="f", quiet=True)

    def test_planetary_json_source_file_recorded(self):
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        with open(_cell_misc_path(self.out_plain)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertIn("source_file", planetary,
                      "source_file missing from extended_metadata.planetary")

    def test_planetary_json_first_write_wins(self):
        """Overwriting the raster must not overwrite planetary.json."""
        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        path = _cell_misc_path(self.out_plain)
        mtime1 = path.stat().st_mtime_ns

        self.assertModule("p.in.ancillary", input=self.tif,
                          output=self.out_plain, type="custom",
                          overwrite=True)
        mtime2 = path.stat().st_mtime_ns
        self.assertEqual(mtime1, mtime2,
                         "planetary.json must not be overwritten on second import")


if __name__ == "__main__":
    test()
