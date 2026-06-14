"""Test of p.matter.bands

Purpose: Detect planetary matter (minerals, ices, gases, organics, liquids)
         from absorption-band depth using a body-aware JSON database.

@author Yann Chemin
"""

import json
import os
import shutil
import subprocess
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
    continuum = 0.30
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
# Uses body="mars" so the module's enum validation passes.
# Species names are prefixed "pmb_test_" to avoid clashing with the real
# matter_bands.json entries when db= is not supplied.

_TEST_DB = {
    "_schema": "matter_bands_v1",
    "bodies": {
        "mars": {
            "minerals": [
                {
                    "name": "pmb_test_mineral_a",
                    "display_name": "PMB Test Mineral A",
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
                    "name": "pmb_test_mineral_b",
                    "display_name": "PMB Test Mineral B (two bands)",
                    "formula": "TB",
                    # detection_range_um starts at 1.70 µm so that the narrow
                    # test group (which covers only ~1.52–1.62 µm) falls
                    # outside: sensor_max (1.621) < dr[0] (1.70) → out of range.
                    "detection_range_um": [1.70, 2.50],
                    "absorption_bands": [
                        {
                            "center": 1.90,
                            "left":   1.75,
                            "right":  2.05,
                            "type":   "primary feature",
                        },
                        {
                            "center": 2.21,
                            "left":   2.10,
                            "right":  2.35,
                            "type":   "confirming feature",
                        },
                    ],
                    "refs": [],
                },
            ],
            "ices":     [],
            "gases":    [],
            "organics": [],
            "liquids":  [],
        }
    },
}

# 30 bands covering 1.0–2.5 µm (full group, step ~0.052 µm)
_WL_UM = [1.0 + i * (1.5 / 29) for i in range(30)]

# Narrow group: bands 10–12 → ~1.517–1.621 µm (does NOT reach 1.70 µm)
_WL_NARROW = _WL_UM[10:13]


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

        # 30 synthetic bands: each pixel carries the Gaussian reflectance value
        # for that band's wavelength (absorption centred at 1.30 µm).
        import numpy as np
        cls.band_names = []
        wl_arr = np.array(_WL_UM)
        refl   = _gaussian_absorption(wl_arr, center_um=1.30)
        for i, wl in enumerate(wl_arr):
            name = f"pmb_test_band_{i:03d}"
            _create_synthetic_band(name, float(refl[i]), cls.region)
            cls.band_names.append(name)

        gs.run_command(
            "i.group",
            group="pmb_test_group",
            input=",".join(cls.band_names),
            overwrite=True, quiet=True,
        )

        # Narrow group: only 3 bands (~1.517–1.621 µm)
        cls.narrow_bands = cls.band_names[10:13]
        gs.run_command(
            "i.group",
            group="pmb_narrow_group",
            input=",".join(cls.narrow_bands),
            overwrite=True, quiet=True,
        )
        cls.narrow_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.narrow_csv, _WL_NARROW)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
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
        """Module exits 0 with --help."""
        # Use subprocess: pygrass rejects unknown flags (h is not declared
        # in the module interface) and blocks before the module even runs.
        result = subprocess.run(
            ["p.matter.bands", "--help"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0,
                         msg=f"--help exited {result.returncode}:\n"
                             f"{result.stderr}")
        self.assertIn("p.matter.bands", result.stdout + result.stderr)

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
        self.assertIn("mars", db["bodies"])
        names = [sp["name"] for sp in db["bodies"]["mars"]["minerals"]]
        self.assertIn("pmb_test_mineral_a", names)
        self.assertIn("pmb_test_mineral_b", names)

    def test_custom_db_list_mode(self):
        """List mode (-l) with a custom db= shows the test species."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
        )
        self.assertModule(module)
        stdout = module.outputs.stdout
        self.assertIn("pmb_test_mineral_a", stdout)
        self.assertIn("pmb_test_mineral_b", stdout)

    # ── Wavelength CSV ────────────────────────────────────────────────────────

    def test_wavelength_csv_band_count_mismatch_fails(self):
        """Module exits non-zero when CSV row count != group band count."""
        bad_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(bad_csv, _WL_UM[:5])  # 5 rows for a 30-band group
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=bad_csv,
            db=self.db_path,
        )
        self.assertModuleFail(module)
        os.unlink(bad_csv)

    def test_narrow_group_skips_out_of_range_species(self):
        """Species requiring wavelengths beyond sensor max appear as out-of-range.

        The narrow group covers ~1.517–1.621 µm.  pmb_test_mineral_b has
        detection_range_um=[1.70, 2.50], so sensor_max (1.621) < 1.70 and
        the species must appear under 'Out of sensor range'.
        """
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_narrow_group",
            body="mars",
            output_prefix="pmb_narrow_out",
            wavelengths=self.narrow_csv,
            db=self.db_path,
        )
        self.assertModule(module)
        self.assertIn("Out of sensor range", module.outputs.stdout)
        self.assertIn("pmb_test_mineral_b", module.outputs.stdout)

    # ── Raster output ─────────────────────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_output_map_created(self):
        """Running with the test group produces at least one output raster."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        self.assertRasterExists("pmb_out_pmb_test_mineral_a")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_output_map_range(self):
        """Output band-depth map values are in [0, 1]."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        stats = gs.parse_command("r.univar", flags="g",
                                 map="pmb_out_pmb_test_mineral_a")
        self.assertGreaterEqual(float(stats["min"]), 0.0)
        self.assertLessEqual(float(stats["max"]), 1.0)

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_min_bd_threshold(self):
        """Raising min_bd to 1.0 produces no valid pixels (BD never reaches 1)."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=self.db_path,
            min_bd=1.0,
            overwrite=True,
        )
        self.assertModule(module)
        # Module should report 0 valid pixels and not write the map at all
        self.assertFalse(
            gs.find_file("pmb_out_pmb_test_mineral_a", element="cell")["name"],
            "Map should not exist when no pixels exceed min_bd=1.0",
        )

    # ── Custom species insertion (round-trip) ─────────────────────────────────

    def test_custom_species_roundtrip(self):
        """A new species added to a local db= copy is detected in -l output."""
        import copy
        custom_db = copy.deepcopy(_TEST_DB)
        custom_db["bodies"]["mars"]["minerals"].append(
            {
                "name": "pmb_roundtrip_species",
                "display_name": "PMB Round-trip test species",
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
                "refs": [{"cite": "Chemin (2026) Test J.", "doi": "10.0000/test"}],
            }
        )
        custom_path = tempfile.mktemp(suffix=".json")
        with open(custom_path, "w") as f:
            json.dump(custom_db, f)

        # Sanity-check: JSON is still valid and species is present
        with open(custom_path) as f:
            loaded = json.load(f)
        names = [sp["name"] for sp in loaded["bodies"]["mars"]["minerals"]]
        self.assertIn("pmb_roundtrip_species", names)

        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_test_group",
            body="mars",
            output_prefix="pmb_out",
            wavelengths=self.wl_csv,
            db=custom_path,
        )
        self.assertModule(module)
        self.assertIn("pmb_roundtrip_species", module.outputs.stdout)

        os.unlink(custom_path)

    # ── Misc/ database path ───────────────────────────────────────────────────

    def test_misc_db_found_before_system(self):
        """A matter_bands.json in $MAPSET/Misc/ takes priority over system db.

        Places a modified database (with a unique species 'pmb_misc_only') in
        the mapset Misc/ directory, then runs without db= and verifies the
        unique species appears in -l output — proving the Misc/ path was used.
        """
        import copy

        env = gs.gisenv()
        misc_dir = os.path.join(
            env["GISDBASE"], env["LOCATION_NAME"], env["MAPSET"], "Misc"
        )
        os.makedirs(misc_dir, exist_ok=True)
        misc_db_path = os.path.join(misc_dir, "matter_bands.json")

        misc_db = copy.deepcopy(_TEST_DB)
        misc_db["bodies"]["mars"]["minerals"].append(
            {
                "name": "pmb_misc_only",
                "display_name": "PMB Misc-only species",
                "formula": "MO",
                "detection_range_um": [1.0, 2.0],
                "absorption_bands": [
                    {"center": 1.30, "left": 1.10, "right": 1.50,
                     "type": "test"}
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
                body="mars",
                output_prefix="pmb_out",
                wavelengths=self.wl_csv,
                # No db= — module must pick up Misc/ automatically
            )
            self.assertModule(module)
            self.assertIn("pmb_misc_only", module.outputs.stdout)
        finally:
            os.unlink(misc_db_path)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
