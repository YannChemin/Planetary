"""
Testsuite for p_meta.py — planetary map metadata Python library.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        libs/p_meta/testsuite/test_p_meta.py -v

Unit tests (class TestPlanetaryMetadataUnit) exercise the PlanetaryMetadata
dataclass in isolation — no GRASS map I/O.  Integration tests
(class TestPlanetaryMetadataGrass) write and read planetary.json through a
live GRASS mapset.
"""

import json
import sys
from pathlib import Path

# Locate p_meta.py relative to this file (../../planetary/p_meta.py).
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "planetary"))

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

from p_meta import (
    METADATA_FILENAME,
    SCHEMA_VERSION,
    PlanetaryMetadata,
    write_planetary_metadata,
)


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _cell_misc_path(mapname: str) -> Path:
    env = gs.gisenv()
    return (
        Path(env["GISDBASE"])
        / env["LOCATION_NAME"]
        / env["MAPSET"]
        / "cell_misc"
        / mapname
        / METADATA_FILENAME
    )


# ---------------------------------------------------------------------------
# Unit tests — pure dataclass, no GRASS map I/O
# ---------------------------------------------------------------------------

class TestPlanetaryMetadataUnit(TestCase):
    """Tests that exercise only the PlanetaryMetadata dataclass methods."""

    # -- construction -------------------------------------------------------

    def test_new_defaults(self):
        meta = PlanetaryMetadata.new()
        self.assertEqual(meta.schema_version, SCHEMA_VERSION)
        self.assertEqual(meta.data_type, "image")
        self.assertIsNotNone(meta.dataset_id)
        self.assertFalse(meta.derived)
        self.assertEqual(meta.wavelength_units, "nm")
        self.assertEqual(meta.n_bands, 1)

    def test_new_kwargs_override(self):
        meta = PlanetaryMetadata.new(
            data_type="dem",
            body="MOON",
            mission="LRO",
            sensor="LROC_NAC",
            radiometric_quantity="elevation",
            radiometric_units="m",
        )
        self.assertEqual(meta.data_type, "dem")
        self.assertEqual(meta.body, "MOON")
        self.assertEqual(meta.mission, "LRO")
        self.assertEqual(meta.sensor, "LROC_NAC")
        self.assertEqual(meta.radiometric_quantity, "elevation")
        self.assertEqual(meta.radiometric_units, "m")

    def test_new_ignores_unknown_kwargs(self):
        # Unknown keyword args should not raise.
        meta = PlanetaryMetadata.new(nonexistent_field="ignored")
        self.assertIsNotNone(meta)

    # -- to_dict top-level keys ---------------------------------------------

    def test_to_dict_has_all_required_keys(self):
        meta = PlanetaryMetadata.new()
        d = meta.to_dict()
        required = (
            "schema_version", "dataset_id", "derived", "data_type",
            "sensor", "wavelength_units", "radiometric_quantity",
            "radiometric_units", "acquisition_datetime",
            "bands", "processing_history", "extended_metadata",
        )
        for key in required:
            self.assertIn(key, d, f"Missing key: {key}")

    def test_to_dict_schema_version(self):
        d = PlanetaryMetadata.new().to_dict()
        self.assertEqual(d["schema_version"], SCHEMA_VERSION)

    def test_to_dict_derived_false_by_default(self):
        d = PlanetaryMetadata.new().to_dict()
        self.assertFalse(d["derived"])

    # -- bands sub-object ---------------------------------------------------

    def test_bands_single_no_wavelengths(self):
        meta = PlanetaryMetadata.new(n_bands=1)
        bands = meta.to_dict()["bands"]
        self.assertEqual(bands["count"], 1)
        self.assertEqual(bands["count_valid"], 1)
        self.assertNotIn("wavelength", bands)
        self.assertNotIn("fwhm", bands)
        self.assertEqual(bands["validity"], [True])

    def test_bands_multi_with_wavelengths(self):
        wl = [450.0, 550.0, 650.0]
        fw = [10.0, 12.0, 11.0]
        meta = PlanetaryMetadata.new(n_bands=3, wavelengths=wl, fwhm=fw)
        bands = meta.to_dict()["bands"]
        self.assertEqual(bands["count"], 3)
        self.assertEqual(bands["wavelength"], wl)
        self.assertEqual(bands["fwhm"], fw)
        self.assertEqual(len(bands["validity"]), 3)
        self.assertTrue(all(bands["validity"]))

    def test_bands_validity_partial(self):
        meta = PlanetaryMetadata.new(n_bands=3, validity=[True, False, True])
        bands = meta.to_dict()["bands"]
        self.assertEqual(bands["validity"], [True, False, True])
        self.assertEqual(bands["count_valid"], 2)

    # -- extended_metadata.planetary block ----------------------------------

    def test_planetary_block_only_non_none_fields(self):
        meta = PlanetaryMetadata.new(body="SATURN", mission="Cassini")
        planetary = meta.to_dict()["extended_metadata"].get("planetary", {})
        self.assertIn("body", planetary)
        self.assertIn("mission", planetary)
        self.assertNotIn("pds_product_id", planetary)
        self.assertNotIn("source_file", planetary)
        self.assertNotIn("spice_kernels", planetary)

    def test_planetary_block_all_fields(self):
        meta = PlanetaryMetadata.new(
            body="MARS",
            mission="MRO",
            pds_product_id="MRO-M-CRISM-001",
            source_file="/tmp/foo.img",
            spice_kernels=["mk.tm", "cassini_v10.tpc"],
        )
        planetary = meta.to_dict()["extended_metadata"]["planetary"]
        self.assertEqual(planetary["body"], "MARS")
        self.assertEqual(planetary["mission"], "MRO")
        self.assertEqual(planetary["pds_product_id"], "MRO-M-CRISM-001")
        self.assertEqual(planetary["source_file"], "/tmp/foo.img")
        self.assertEqual(planetary["spice_kernels"], ["mk.tm", "cassini_v10.tpc"])

    def test_planetary_block_absent_when_no_fields(self):
        meta = PlanetaryMetadata.new()
        planetary = meta.to_dict()["extended_metadata"].get("planetary", {})
        self.assertEqual(planetary, {})

    # -- processing history -------------------------------------------------

    def test_add_history_entry_structure(self):
        meta = PlanetaryMetadata.new()
        meta.add_history_entry("p.in.dem input=foo.tif output=bar")
        history = meta.to_dict()["processing_history"]
        self.assertEqual(len(history), 1)
        entry = history[0]
        self.assertIn("command", entry)
        self.assertIn("timestamp", entry)
        self.assertIn("inputs", entry)
        self.assertIn("outputs", entry)
        self.assertIn("p.in.dem", entry["command"])

    def test_add_history_custom_timestamp(self):
        meta = PlanetaryMetadata.new()
        meta.add_history_entry("cmd", timestamp="2004-07-01T00:00:00Z")
        ts = meta.to_dict()["processing_history"][0]["timestamp"]
        self.assertEqual(ts, "2004-07-01T00:00:00Z")

    # -- round-trip ---------------------------------------------------------

    def test_round_trip_all_fields(self):
        meta = PlanetaryMetadata.new(
            data_type="rings",
            sensor="ISS_NAC",
            mission="Cassini",
            body="SATURN",
            acquisition_datetime="2004-07-01T03:11:40Z",
            radiometric_quantity="raw_dn",
            radiometric_units="DN",
            n_bands=1,
            wavelengths=[650.0],
            fwhm=[10.0],
        )
        meta.add_history_entry("p.in.rings output=b_ring")
        d = meta.to_dict()
        meta2 = PlanetaryMetadata._from_dict(d)
        self.assertEqual(meta2.data_type, "rings")
        self.assertEqual(meta2.sensor, "ISS_NAC")
        self.assertEqual(meta2.body, "SATURN")
        self.assertEqual(meta2.mission, "Cassini")
        self.assertEqual(meta2.acquisition_datetime, "2004-07-01T03:11:40Z")
        self.assertEqual(meta2.radiometric_quantity, "raw_dn")
        self.assertEqual(meta2.radiometric_units, "DN")
        self.assertEqual(meta2.wavelengths, [650.0])
        self.assertEqual(meta2.fwhm, [10.0])
        self.assertEqual(len(meta2.processing_history), 1)

    def test_round_trip_preserves_extended_metadata(self):
        meta = PlanetaryMetadata.new()
        meta.extended_metadata["custom_tool"] = {"version": "2.0"}
        d = meta.to_dict()
        meta2 = PlanetaryMetadata._from_dict(d)
        self.assertIn("custom_tool", meta2.extended_metadata)
        self.assertEqual(meta2.extended_metadata["custom_tool"]["version"], "2.0")

    # -- uniqueness ---------------------------------------------------------

    def test_dataset_id_is_unique(self):
        ids = {PlanetaryMetadata.new().dataset_id for _ in range(50)}
        self.assertEqual(len(ids), 50, "dataset_id must be unique per instance")


# ---------------------------------------------------------------------------
# Integration tests — write/read in a live GRASS mapset
# ---------------------------------------------------------------------------

class TestPlanetaryMetadataGrass(TestCase):
    """Tests that create GRASS maps and verify planetary.json on disk."""

    src = "pmeta_test_src"
    out = "pmeta_test_out"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=100, s=0, e=100, w=0, res=10)
        gs.mapcalc(f"{cls.src} = row() + col()", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        gs.run_command(
            "g.remove", type="raster",
            name=f"{cls.src},{cls.out}",
            flags="f", quiet=True,
        )
        cls.del_temp_region()

    def setUp(self):
        # Fresh copy of the source map for each test.
        gs.run_command("g.copy", raster=f"{self.src},{self.out}", overwrite=True)

    def tearDown(self):
        gs.run_command(
            "g.remove", type="raster", name=self.out, flags="f", quiet=True
        )

    # -- file creation -------------------------------------------------------

    def test_write_creates_json_file(self):
        write_planetary_metadata(self.out, module="test", data_type="image",
                                  body="TESTBODY")
        self.assertTrue(PlanetaryMetadata.exists(self.out))
        self.assertTrue(_cell_misc_path(self.out).exists())

    def test_written_json_is_parseable(self):
        write_planetary_metadata(self.out, module="test", data_type="image",
                                  body="PARSEABLE")
        path = _cell_misc_path(self.out)
        with open(path) as fh:
            data = json.load(fh)
        self.assertEqual(data["schema_version"], SCHEMA_VERSION)

    # -- field values in written JSON ----------------------------------------

    def test_written_json_data_type(self):
        write_planetary_metadata(self.out, module="test", data_type="dem")
        path = _cell_misc_path(self.out)
        with open(path) as fh:
            data = json.load(fh)
        self.assertEqual(data["data_type"], "dem")

    def test_written_json_planetary_block(self):
        write_planetary_metadata(
            self.out, module="test", data_type="image",
            body="MOON", mission="LRO", pds_product_id="TEST-001",
            source_file="/tmp/test.img",
        )
        path = _cell_misc_path(self.out)
        with open(path) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertEqual(planetary.get("body"), "MOON")
        self.assertEqual(planetary.get("mission"), "LRO")
        self.assertEqual(planetary.get("pds_product_id"), "TEST-001")
        self.assertEqual(planetary.get("source_file"), "/tmp/test.img")

    def test_written_json_bands(self):
        write_planetary_metadata(self.out, module="test", n_bands=3,
                                  wavelengths=[450.0, 550.0, 650.0],
                                  fwhm=[10.0, 12.0, 11.0])
        path = _cell_misc_path(self.out)
        with open(path) as fh:
            data = json.load(fh)
        bands = data["bands"]
        self.assertEqual(bands["count"], 3)
        self.assertEqual(bands["wavelength"], [450.0, 550.0, 650.0])
        self.assertEqual(bands["fwhm"], [10.0, 12.0, 11.0])

    def test_written_json_processing_history(self):
        write_planetary_metadata(self.out, module="p.in.test",
                                  command="p.in.test input=a output=b")
        path = _cell_misc_path(self.out)
        with open(path) as fh:
            data = json.load(fh)
        history = data.get("processing_history", [])
        self.assertTrue(len(history) >= 1)
        self.assertIn("p.in.test", history[0]["command"])

    # -- load round-trip -----------------------------------------------------

    def test_load_round_trip(self):
        write_planetary_metadata(
            self.out, module="test",
            data_type="dem", body="MOON", mission="LRO",
            radiometric_quantity="elevation", radiometric_units="m",
            acquisition_datetime="2024-01-15T12:00:00Z",
        )
        meta = PlanetaryMetadata.load(self.out)
        self.assertEqual(meta.data_type, "dem")
        self.assertEqual(meta.body, "MOON")
        self.assertEqual(meta.mission, "LRO")
        self.assertEqual(meta.radiometric_quantity, "elevation")
        self.assertEqual(meta.radiometric_units, "m")
        self.assertEqual(meta.acquisition_datetime, "2024-01-15T12:00:00Z")

    def test_load_raises_if_missing(self):
        with self.assertRaises(FileNotFoundError):
            PlanetaryMetadata.load("__nonexistent_map__")

    # -- first-write-wins semantics ------------------------------------------

    def test_first_write_wins(self):
        write_planetary_metadata(self.out, module="first", body="FIRST")
        path = _cell_misc_path(self.out)
        mtime1 = path.stat().st_mtime_ns

        write_planetary_metadata(self.out, module="second", body="SECOND")
        mtime2 = path.stat().st_mtime_ns

        self.assertEqual(mtime1, mtime2, "Second write must not modify the file")
        meta = PlanetaryMetadata.load(self.out)
        self.assertEqual(meta.body, "FIRST")

    def test_exists_false_before_write(self):
        self.assertFalse(PlanetaryMetadata.exists(self.out))


if __name__ == "__main__":
    test()
