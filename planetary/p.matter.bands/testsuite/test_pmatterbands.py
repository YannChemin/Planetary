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

# ── Phase 2 test database ─────────────────────────────────────────────────────
# Contains one UV reflectance species and one MIR emissivity species.
# The mode= field on the MIR species is the key field under test.

_TEST_DB_P2 = {
    "_schema": "matter_bands_v1",
    "bodies": {
        "mars": {
            "minerals": [
                {
                    "name": "pmb_p2_uv_mineral",
                    "display_name": "PMB Phase-2 UV mineral",
                    "formula": "UV",
                    # mode defaults to "reflectance" when absent
                    "detection_range_um": [0.18, 0.40],
                    "absorption_bands": [
                        {
                            "center": 0.22,
                            "left":   0.18,
                            "right":  0.28,
                            "type":   "electronic_charge_transfer",
                        }
                    ],
                    "refs": [],
                },
                {
                    "name": "pmb_p2_mir_mineral",
                    "display_name": "PMB Phase-2 MIR emissivity mineral",
                    "formula": "MIR",
                    "mode": "emissivity",
                    "detection_range_um": [8.5, 12.0],
                    "absorption_bands": [
                        {
                            "center": 9.3,
                            "left":   8.6,
                            "right":  10.0,
                            "type":   "Si-O_stretching",
                        }
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

# UV sensor: 20 bands 0.18–0.45 µm (step ~0.014 µm)
_WL_UV = [0.18 + i * (0.27 / 19) for i in range(20)]

# MIR sensor: 20 bands 8.0–12.0 µm (step ~0.21 µm)
_WL_MIR = [8.0 + i * (4.0 / 19) for i in range(20)]

# FIR sensor: wavelengths around the cometary H2O rotational lines
_WL_FIR = [50.0 + i * (160.0 / 19) for i in range(20)]  # 50–210 µm

# Phase-2 comet database: reflectance UV gas species + FIR ice species
_TEST_DB_COMET = {
    "_schema": "matter_bands_v1",
    "bodies": {
        "comet": {
            "minerals": [],
            "ices": [
                {
                    "name": "pmb_p2_h2o_fir",
                    "display_name": "PMB H2O FIR rotational",
                    "formula": "H2O",
                    "detection_range_um": [50.0, 200.0],
                    "absorption_bands": [
                        {"center": 56.9,  "left": 55.0,  "right": 59.0,
                         "type": "rotational"},
                        {"center": 179.5, "left": 176.0, "right": 183.0,
                         "type": "rotational"},
                    ],
                    "refs": [],
                },
            ],
            "gases": [
                {
                    "name": "pmb_p2_oh_coma",
                    "display_name": "PMB OH UV",
                    "formula": "OH",
                    "detection_range_um": [0.28, 0.33],
                    "absorption_bands": [
                        {"center": 0.308, "left": 0.290, "right": 0.320,
                         "type": "electronic"},
                    ],
                    "refs": [],
                },
            ],
            "organics": [],
            "liquids":  [],
        }
    },
}


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


class TestPmatterbandsPhase2(TestCase):
    """Phase 2 tests: UV/MIR emissivity/FIR wavelength range extension."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)
        cls.region = gs.region()

        # Write Phase 2 test databases
        cls.db_p2   = tempfile.mktemp(suffix=".json")
        cls.db_comet = tempfile.mktemp(suffix=".json")
        with open(cls.db_p2, "w") as f:
            json.dump(_TEST_DB_P2, f)
        with open(cls.db_comet, "w") as f:
            json.dump(_TEST_DB_COMET, f)

        # CSV files for each sensor range
        cls.wl_uv_csv  = tempfile.mktemp(suffix=".csv")
        cls.wl_mir_csv = tempfile.mktemp(suffix=".csv")
        cls.wl_fir_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_uv_csv,  _WL_UV)
        _write_wavelength_csv(cls.wl_mir_csv, _WL_MIR)
        _write_wavelength_csv(cls.wl_fir_csv, _WL_FIR)

        import numpy as np

        # UV group: Gaussian absorption at 0.22 µm (UV mineral feature)
        cls.uv_bands = []
        wl_arr_uv = np.array(_WL_UV)
        refl_uv = _gaussian_absorption(wl_arr_uv, center_um=0.22,
                                        depth=0.5, fwhm_um=0.02)
        for i, wl in enumerate(wl_arr_uv):
            name = f"pmb_p2_uv_band_{i:03d}"
            _create_synthetic_band(name, float(refl_uv[i]), cls.region)
            cls.uv_bands.append(name)
        gs.run_command("i.group", group="pmb_p2_uv_group",
                       input=",".join(cls.uv_bands),
                       overwrite=True, quiet=True)

        # MIR group: Gaussian absorption at 9.3 µm (emissivity dip in TIR)
        # Emissivity data: high values (~0.97) with a reststrahlen dip
        cls.mir_bands = []
        wl_arr_mir = np.array(_WL_MIR)
        emiss_mir = _gaussian_absorption(wl_arr_mir, center_um=9.3,
                                          depth=0.35, fwhm_um=0.4)
        for i, wl in enumerate(wl_arr_mir):
            name = f"pmb_p2_mir_band_{i:03d}"
            _create_synthetic_band(name, float(emiss_mir[i]), cls.region)
            cls.mir_bands.append(name)
        gs.run_command("i.group", group="pmb_p2_mir_group",
                       input=",".join(cls.mir_bands),
                       overwrite=True, quiet=True)

        # FIR group: Gaussian absorption at 57 µm (H2O rotational line)
        cls.fir_bands = []
        wl_arr_fir = np.array(_WL_FIR)
        refl_fir = _gaussian_absorption(wl_arr_fir, center_um=56.9,
                                         depth=0.45, fwhm_um=1.5)
        for i, wl in enumerate(wl_arr_fir):
            name = f"pmb_p2_fir_band_{i:03d}"
            _create_synthetic_band(name, float(refl_fir[i]), cls.region)
            cls.fir_bands.append(name)
        gs.run_command("i.group", group="pmb_p2_fir_group",
                       input=",".join(cls.fir_bands),
                       overwrite=True, quiet=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        for bands in [cls.uv_bands, cls.mir_bands, cls.fir_bands]:
            for name in bands:
                gs.run_command("g.remove", flags="f", type="raster",
                               name=name, quiet=True)
        for pat in ["pmb_p2_uv_out_*", "pmb_p2_mir_out_*", "pmb_p2_fir_out_*"]:
            gs.run_command("g.remove", flags="f", type="raster",
                           pattern=pat, quiet=True)
        for tmp in [cls.db_p2, cls.db_comet,
                    cls.wl_uv_csv, cls.wl_mir_csv, cls.wl_fir_csv]:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    # ── mode= parameter ───────────────────────────────────────────────────────

    def test_default_mode_is_reflectance(self):
        """Without mode=, the module defaults to reflectance and mode label appears."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_uv_group",
            body="mars",
            output_prefix="pmb_p2_uv_out",
            wavelengths=self.wl_uv_csv,
            db=self.db_p2,
        )
        self.assertModule(module)
        out = module.outputs.stdout + module.outputs.stderr
        self.assertIn("Mode: reflectance", out)

    def test_emissivity_mode_label_in_output(self):
        """`mode=emissivity` is reported in the processing summary."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_mir_group",
            body="mars",
            output_prefix="pmb_p2_mir_out",
            wavelengths=self.wl_mir_csv,
            mode="emissivity",
            db=self.db_p2,
        )
        self.assertModule(module)
        out = module.outputs.stdout + module.outputs.stderr
        self.assertIn("Mode: emissivity", out)

    def test_reflectance_mode_skips_emissivity_species(self):
        """In reflectance mode, species tagged mode=emissivity are excluded from in-range list.

        The MIR sensor (8–12 µm) covers pmb_p2_mir_mineral's wavelengths, but
        since its mode=emissivity it must be skipped when running in default
        reflectance mode — it should not appear in the detectable list.
        """
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_mir_group",
            body="mars",
            output_prefix="pmb_p2_mir_out",
            wavelengths=self.wl_mir_csv,
            # mode defaults to reflectance
            db=self.db_p2,
        )
        self.assertModule(module)
        # pmb_p2_mir_mineral must NOT appear as detectable
        self.assertNotIn("pmb_p2_mir_mineral",
                         module.outputs.stdout.split("Out of sensor range")[0])

    def test_emissivity_mode_skips_reflectance_species(self):
        """In emissivity mode, species without mode=emissivity tag are excluded.

        The UV sensor (0.18–0.45 µm) covers pmb_p2_uv_mineral, but since it
        has no mode tag (= reflectance), it must be skipped in emissivity mode.
        """
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_uv_group",
            body="mars",
            output_prefix="pmb_p2_uv_out",
            wavelengths=self.wl_uv_csv,
            mode="emissivity",
            db=self.db_p2,
        )
        self.assertModule(module)
        self.assertNotIn("pmb_p2_uv_mineral",
                         module.outputs.stdout.split("Out of sensor range")[0])

    # ── UV range ──────────────────────────────────────────────────────────────

    def test_uv_species_detectable_with_uv_sensor(self):
        """UV species (0.22 µm) appears as detectable with a UV-range sensor."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_uv_group",
            body="mars",
            output_prefix="pmb_p2_uv_out",
            wavelengths=self.wl_uv_csv,
            db=self.db_p2,
        )
        self.assertModule(module)
        # Should be in the detectable section (before "Out of sensor range")
        stdout = module.outputs.stdout
        detectable_section = stdout.split("Out of sensor range")[0]
        self.assertIn("pmb_p2_uv_mineral", detectable_section)

    def test_uv_species_out_of_range_with_swir_sensor(self):
        """UV species is listed as out-of-range when sensor starts at 1.0 µm."""
        # Reuse the Phase 1 SWIR group (1.0–2.5 µm) from TestPmatterbands,
        # but we need a group here — create a minimal 3-band SWIR group.
        import numpy as np
        wl_swir = [1.0, 1.5, 2.0]
        swir_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(swir_csv, wl_swir)
        swir_bands = []
        for i, wl in enumerate(wl_swir):
            name = f"pmb_p2_swir_tmp_{i}"
            _create_synthetic_band(name, 0.25, self.region)
            swir_bands.append(name)
        gs.run_command("i.group", group="pmb_p2_swir_group",
                       input=",".join(swir_bands),
                       overwrite=True, quiet=True)
        try:
            module = SimpleModule(
                "p.matter.bands",
                flags="l",
                group="pmb_p2_swir_group",
                body="mars",
                output_prefix="pmb_p2_uv_out",
                wavelengths=swir_csv,
                db=self.db_p2,
            )
            self.assertModule(module)
            self.assertIn("Out of sensor range", module.outputs.stdout)
            self.assertIn("pmb_p2_uv_mineral", module.outputs.stdout)
        finally:
            os.unlink(swir_csv)
            for name in swir_bands:
                gs.run_command("g.remove", flags="f", type="raster",
                               name=name, quiet=True)
            gs.run_command("g.remove", flags="f", type="group",
                           name="pmb_p2_swir_group", quiet=True)

    # ── MIR emissivity range ──────────────────────────────────────────────────

    def test_mir_species_detectable_in_emissivity_mode(self):
        """MIR species at 9.3 µm appears as detectable with MIR sensor + mode=emissivity."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_mir_group",
            body="mars",
            output_prefix="pmb_p2_mir_out",
            wavelengths=self.wl_mir_csv,
            mode="emissivity",
            db=self.db_p2,
        )
        self.assertModule(module)
        detectable_section = module.outputs.stdout.split("Out of sensor range")[0]
        self.assertIn("pmb_p2_mir_mineral", detectable_section)

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_emissivity_output_map_created(self):
        """Running in emissivity mode produces a valid band-depth output raster."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_p2_mir_group",
            body="mars",
            output_prefix="pmb_p2_mir_out",
            wavelengths=self.wl_mir_csv,
            mode="emissivity",
            db=self.db_p2,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        self.assertRasterExists("pmb_p2_mir_out_pmb_p2_mir_mineral")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed — skipping output tests")
    def test_emissivity_output_map_range(self):
        """Emissivity band-depth map values are in [0, 1]."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_p2_mir_group",
            body="mars",
            output_prefix="pmb_p2_mir_out",
            wavelengths=self.wl_mir_csv,
            mode="emissivity",
            db=self.db_p2,
            min_bd=0.001,
            overwrite=True,
        )
        self.assertModule(module)
        stats = gs.parse_command("r.univar", flags="g",
                                 map="pmb_p2_mir_out_pmb_p2_mir_mineral")
        self.assertGreaterEqual(float(stats["min"]), 0.0)
        self.assertLessEqual(float(stats["max"]), 1.0)

    # ── FIR range ─────────────────────────────────────────────────────────────

    def test_fir_species_detectable_with_fir_sensor(self):
        """FIR H2O rotational species (56.9 µm) appears as detectable with FIR sensor."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_fir_group",
            body="comet",
            output_prefix="pmb_p2_fir_out",
            wavelengths=self.wl_fir_csv,
            db=self.db_comet,
        )
        self.assertModule(module)
        detectable_section = module.outputs.stdout.split("Out of sensor range")[0]
        self.assertIn("pmb_p2_h2o_fir", detectable_section)

    def test_fir_uv_species_out_of_range_with_fir_sensor(self):
        """UV OH coma species is out-of-range when sensor covers only FIR (50–210 µm)."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p2_fir_group",
            body="comet",
            output_prefix="pmb_p2_fir_out",
            wavelengths=self.wl_fir_csv,
            db=self.db_comet,
        )
        self.assertModule(module)
        self.assertIn("Out of sensor range", module.outputs.stdout)
        self.assertIn("pmb_p2_oh_coma", module.outputs.stdout)

    # ── Phase 2 database content ──────────────────────────────────────────────

    def test_phase2_db_has_uv_entries(self):
        """Real matter_bands.json contains UV entries (< 0.4 µm) for expected bodies."""
        import json as _json
        import os as _os
        gisbase = _os.getenv("GISBASE", "")
        sys_db = _os.path.join(gisbase, "etc", "planetary", "matter_bands.json")
        dev_db = _os.path.normpath(
            _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                          "..", "..", "..", "data", "matter_bands.json"))
        db_path = sys_db if _os.path.isfile(sys_db) else dev_db
        if not _os.path.isfile(db_path):
            self.skipTest("matter_bands.json not found (not installed and no dev tree)")
        with open(db_path) as f:
            db = _json.load(f)
        uv_centers = []
        for bdata in db["bodies"].values():
            for mtype in ["minerals", "ices", "gases", "organics", "liquids"]:
                for sp in bdata.get(mtype, []):
                    for b in sp.get("absorption_bands", []):
                        if b["center"] < 0.4:
                            uv_centers.append(b["center"])
        self.assertGreater(len(uv_centers), 0,
                           "No UV (<0.4 µm) band entries found in matter_bands.json")
        self.assertGreaterEqual(len(uv_centers), 10,
                                f"Expected ≥10 UV entries, got {len(uv_centers)}")

    def test_phase2_db_has_mir_emissivity_entries(self):
        """Real matter_bands.json contains emissivity-mode MIR entries (5–30 µm)."""
        import json as _json
        import os as _os
        gisbase = _os.getenv("GISBASE", "")
        sys_db = _os.path.join(gisbase, "etc", "planetary", "matter_bands.json")
        dev_db = _os.path.normpath(
            _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                          "..", "..", "..", "data", "matter_bands.json"))
        db_path = sys_db if _os.path.isfile(sys_db) else dev_db
        if not _os.path.isfile(db_path):
            self.skipTest("matter_bands.json not found")
        with open(db_path) as f:
            db = _json.load(f)
        mir_emiss = []
        for bdata in db["bodies"].values():
            for mtype in ["minerals", "ices", "gases", "organics", "liquids"]:
                for sp in bdata.get(mtype, []):
                    if sp.get("mode") != "emissivity":
                        continue
                    for b in sp.get("absorption_bands", []):
                        if 5.0 <= b["center"] <= 30.0:
                            mir_emiss.append(b["center"])
        self.assertGreater(len(mir_emiss), 0,
                           "No emissivity-mode MIR entries in matter_bands.json")
        self.assertGreaterEqual(len(mir_emiss), 10,
                                f"Expected ≥10 MIR emissivity entries, got {len(mir_emiss)}")

    def test_phase2_db_has_fir_entries(self):
        """Real matter_bands.json contains FIR entries (> 30 µm)."""
        import json as _json
        import os as _os
        gisbase = _os.getenv("GISBASE", "")
        sys_db = _os.path.join(gisbase, "etc", "planetary", "matter_bands.json")
        dev_db = _os.path.normpath(
            _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                          "..", "..", "..", "data", "matter_bands.json"))
        db_path = sys_db if _os.path.isfile(sys_db) else dev_db
        if not _os.path.isfile(db_path):
            self.skipTest("matter_bands.json not found")
        with open(db_path) as f:
            db = _json.load(f)
        fir_centers = []
        for bdata in db["bodies"].values():
            for mtype in ["minerals", "ices", "gases", "organics", "liquids"]:
                for sp in bdata.get(mtype, []):
                    for b in sp.get("absorption_bands", []):
                        if b["center"] > 30.0:
                            fir_centers.append(b["center"])
        self.assertGreater(len(fir_centers), 0,
                           "No FIR (>30 µm) entries found in matter_bands.json")

    def test_phase2_db_mode_field_valid(self):
        """Every species with an explicit mode= field has a valid value."""
        import json as _json
        import os as _os
        gisbase = _os.getenv("GISBASE", "")
        sys_db = _os.path.join(gisbase, "etc", "planetary", "matter_bands.json")
        dev_db = _os.path.normpath(
            _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                          "..", "..", "..", "data", "matter_bands.json"))
        db_path = sys_db if _os.path.isfile(sys_db) else dev_db
        if not _os.path.isfile(db_path):
            self.skipTest("matter_bands.json not found")
        with open(db_path) as f:
            db = _json.load(f)
        valid_modes = {"reflectance", "emissivity"}
        invalid = []
        for body, bdata in db["bodies"].items():
            for mtype in ["minerals", "ices", "gases", "organics", "liquids"]:
                for sp in bdata.get(mtype, []):
                    m = sp.get("mode")
                    if m is not None and m not in valid_modes:
                        invalid.append(f"{body}/{sp['name']}: mode={m!r}")
        self.assertEqual(invalid, [],
                         "Invalid mode values in matter_bands.json:\n" +
                         "\n".join(invalid))


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
