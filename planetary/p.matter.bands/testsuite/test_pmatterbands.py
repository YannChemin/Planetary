"""Test of p.matter.bands

Purpose: Detect planetary matter (minerals, ices, gases, organics, liquids)
         from absorption-band depth using a body-aware JSON database.

@author Yann Chemin
"""

import json
import os
import shutil
import tempfile
import unittest

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


# ── Synthetic absorption spectrum helpers ─────────────────────────────────────

def _gaussian_absorption(wavelengths_um, center_um, depth=0.4, fwhm_um=0.05):
    """Return reflectance array (numpy) with one Gaussian absorption feature."""
    import numpy as np
    sigma = fwhm_um / (2.0 * (2.0 * np.log(2.0)) ** 0.5)
    continuum = 0.30  # flat reflectance baseline
    absorb = depth * np.exp(-0.5 * ((wavelengths_um - center_um) / sigma) ** 2)
    return continuum * (1.0 - absorb)


def _write_wavelength_csv(path, wavelengths_um, fwhm_um=0.005):
    """Write a two-column wavelength CSV file."""
    with open(path, "w") as f:
        f.write("# wavelength_um,fwhm_um\n")
        for wl in wavelengths_um:
            f.write(f"{wl:.6f},{fwhm_um:.6f}\n")


def _create_synthetic_band(mapname, value_or_array, region, overwrite=True):
    """Create a GRASS FCELL raster from a scalar or 2-D numpy array."""
    import numpy as np

    nr = int(region["rows"])
    nc = int(region["cols"])

    if np.isscalar(value_or_array):
        arr = np.full((nr, nc), value_or_array, dtype=np.float32)
    else:
        arr = np.asarray(value_or_array, dtype=np.float32)

    tmp = tempfile.mktemp(suffix=".bin")
    arr.tofile(tmp)
    gs.run_command(
        "r.in.bin",
        input=tmp, output=mapname,
        bytes=4, flags="f",
        north=region["n"], south=region["s"],
        east=region["e"],  west=region["w"],
        rows=nr, cols=nc,
        overwrite=overwrite, quiet=True,
    )
    os.unlink(tmp)


# ── Minimal test database ─────────────────────────────────────────────────────

_TEST_DB = {
    "_schema": "matter_bands_v1",
    "bodies": {
        "testbody": {
            "minerals": [
                {
                    "name": "test_mineral_a",
                    "display_name": "Test Mineral A",
                    "formula": "TA",
                    "detection_range_um": [1.0, 2.0],
                    "absorption_bands": [
                        {
                            "center": 1.30,
                            "left":   1.10,
                            "right":  1.50,
                            "type":   "test feature",
                        }
                    ],
                    "refs": [],
                },
                {
                    "name": "test_mineral_b",
                    "display_name": "Test Mineral B (two bands)",
                    "formula": "TB",
                    "detection_range_um": [1.0, 2.5],
                    "absorption_bands": [
                        {
                            "center": 1.30,
                            "left":   1.10,
                            "right":  1.50,
                            "type":   "primary feature",
                        },
                        {
                            "center": 1.90,
                            "left":   1.75,
                            "right":  2.05,
                            "type":   "confirming feature",
                        },
                    ],
                    "refs": [],
                },
            ],
            "ices": [],
            "gases": [],
            "organics": [],
            "liquids": [],
        }
    },
}

# Wavelengths covering the test features (1.0 – 2.5 µm, 30 bands)
_WL_UM = [1.0 + i * (1.5 / 29) for i in range(30)]


class TestPmatterbands(TestCase):
    """Test suite for p.matter.bands."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)

        cls.region = gs.region()
        cls.db_path = tempfile.mktemp(suffix=".json")
        cls.wl_csv  = tempfile.mktemp(suffix=".csv")

        with open(cls.db_path, "w") as f:
            json.dump(_TEST_DB, f)
        _write_wavelength_csv(cls.wl_csv, _WL_UM)

        # Build GRASS image group with one synthetic band per wavelength
        import numpy as np
        cls.band_names = []
        wl_arr = np.array(_WL_UM)
        for i, wl in enumerate(wl_arr):
            name = f"pmb_test_band_{i:03d}"
            reflectance = _gaussian_absorption(wl_arr, center_um=1.30)[i]
            _create_synthetic_band(name, float(reflectance), cls.region)
            cls.band_names.append(name)

        gs.run_command(
            "i.group",
            group="pmb_test_group",
            input=",".join(cls.band_names),
            overwrite=True, quiet=True,
        )

        # Separate group with only 3 bands — exercises single-band path
        cls.narrow_bands = cls.band_names[10:13]
        gs.run_command(
            "i.group",
            group="pmb_narrow_group",
            input=",".join(cls.narrow_bands),
            overwrite=True, quiet=True,
        )
        cls.narrow_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.narrow_csv, _WL_UM[10:13])

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        # Remove synthetic rasters
        for name in cls.band_names:
            gs.run_command("g.remove", flags="f", type="raster",
                           name=name, quiet=True)
        gs.run_command("g.remove", flags="f", type="raster",
                       pattern="pmb_out_*", quiet=True)
        gs.run_command("g.remove", flags="f", type="raster",
                       pattern="pmb_narrow_out_*", quiet=True)
        for tmp in [cls.db_path, cls.wl_csv, cls.narrow_csv]:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    # ── Infrastructure ────────────────────────────────────────────────────────

    def test_help(self):
        """Module exits successfully with --help."""
        module = SimpleModule("p.matter.bands", flags="h")
        self.assertModule(module)

    def test_module_in_path(self):
        """Module binary is found in PATH."""
        self.assertIsNotNone(
            shutil.which("p.matter.bands"),
            "p.matter.bands not found in PATH — is it installed?",
        )

    # ── Database loading ──────────────────────────────────────────────────────

    def test_db_json_valid(self):
        """Test database JSON file is valid and has expected schema."""
        with open(self.db_path) as f:
            db = json.load(f)
        self.assertIn("bodies", db)
        self.assertIn("testbody", db["bodies"])

    def test_custom_db_list_mode(self):
        """List mode (-l) with a custom db= shows the test species."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
        )
        self.assertModule(module)
        stdout = module.outputs.stdout
        self.assertIn("test_mineral_a", stdout)
        self.assertIn("test_mineral_b", stdout)

    # ── Wavelength CSV ────────────────────────────────────────────────────────

    def test_wavelength_csv_band_count_mismatch_fails(self):
        """Module must exit non-zero when CSV row count != group band count."""
        bad_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(bad_csv, _WL_UM[:5])  # only 5 rows for 30 bands
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=bad_csv,
            db=self.db_path,
        )
        self.assertModuleFail(module)
        os.unlink(bad_csv)

    def test_narrow_group_skips_out_of_range_species(self):
        """A group covering only 3 bands should skip species needing wider range."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_narrow_group",
            body="testbody",
            output_prefix="pmb_narrow_out",
            wavelengths=self.narrow_csv,
            db=self.db_path,
        )
        self.assertModule(module)
        stdout = module.outputs.stdout
        # test_mineral_b requires 1.0–2.5 µm; narrow group is ~1.5–1.6 µm
        self.assertIn("Out of sensor range", stdout)

    # ── Raster output ─────────────────────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_output_map_created(self):
        """Running with the test group produces at least one output raster."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        # At least test_mineral_a should be written
        self.assertRasterExists("pmb_out_test_mineral_a")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_output_map_range(self):
        """Output band-depth map values are in [0, 1]."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        stats = gs.parse_command(
            "r.univar",
            flags="g",
            map="pmb_out_test_mineral_a",
        )
        self.assertGreaterEqual(float(stats["min"]), 0.0)
        self.assertLessEqual(float(stats["max"]), 1.0)

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_min_bd_threshold(self):
        """Raising min_bd to 1.0 should produce no valid (non-NULL) pixels."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=1.0,
            overwrite=True,
        )
        self.assertModule(module)
        # Map may not exist at all (no pixels exceed BD=1.0), which is correct
        if gs.find_file("pmb_out_test_mineral_a", element="cell")["name"]:
            stats = gs.parse_command(
                "r.univar", flags="g", map="pmb_out_test_mineral_a"
            )
            self.assertEqual(int(stats.get("n", 0)), 0)

    # ── Custom species insertion (round-trip) ─────────────────────────────────

    def test_custom_species_roundtrip(self):
        """A new species added to a local db= copy is detected in -l output."""
        import copy
        custom_db = copy.deepcopy(_TEST_DB)
        custom_db["bodies"]["testbody"]["minerals"].append(
            {
                "name": "roundtrip_species",
                "display_name": "Round-trip test species",
                "formula": "RT",
                "detection_range_um": [1.2, 1.5],
                "absorption_bands": [
                    {
                        "center": 1.30,
                        "left":   1.20,
                        "right":  1.45,
                        "type":   "synthetic test feature",
                    }
                ],
                "refs": [
                    {
                        "cite": "Chemin (2026) Test J.",
                        "doi":  "10.0000/test",
                    }
                ],
            }
        )
        custom_path = tempfile.mktemp(suffix=".json")
        with open(custom_path, "w") as f:
            json.dump(custom_db, f)

        # Validate JSON is still well-formed after insertion
        with open(custom_path) as f:
            loaded = json.load(f)
        names = [sp["name"] for sp in loaded["bodies"]["testbody"]["minerals"]]
        self.assertIn("roundtrip_species", names)

        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="testbody",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=custom_path,
        )
        self.assertModule(module)
        self.assertIn("roundtrip_species", module.outputs.stdout)

        os.unlink(custom_path)

    # ── Misc/ database path ───────────────────────────────────────────────────

    def test_misc_db_found_before_system(self):
        """
        A matter_bands.json placed in $MAPSET/Misc/ takes priority over db=.
        Verify the search-path ordering by placing a modified db there and
        checking that the unique species in it appears in -l output.
        """
        import copy

        env = gs.gisenv()
        misc_dir = os.path.join(
            env["GISDBASE"], env["LOCATION_NAME"], env["MAPSET"], "Misc"
        )
        os.makedirs(misc_dir, exist_ok=True)
        misc_db_path = os.path.join(misc_dir, "matter_bands.json")

        # Write a db with an extra species that the system db does not have
        misc_db = copy.deepcopy(_TEST_DB)
        misc_db["bodies"]["testbody"]["minerals"].append(
            {
                "name": "misc_only_species",
                "display_name": "Misc-only species",
                "formula": "MO",
                "detection_range_um": [1.0, 2.0],
                "absorption_bands": [
                    {"center": 1.30, "left": 1.10, "right": 1.50, "type": "test"}
                ],
                "refs": [],
            }
        )
        with open(misc_db_path, "w") as f:
            json.dump(misc_db, f)

        try:
            module = SimpleModule(
                "p.matter.bands",
                flags="l",
                group="pmb_test_group",
                body="testbody",
                output_prefix="pmb_out",
                wavelengths=self.wl_csv,
                # No db= — must pick up from Misc/ automatically
            )
            self.assertModule(module)
            self.assertIn("misc_only_species", module.outputs.stdout)
        finally:
            os.unlink(misc_db_path)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
