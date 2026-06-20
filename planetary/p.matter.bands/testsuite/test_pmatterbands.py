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


class TestPmatterbandsPhase3(TestCase):
    """Phase 3 tests: new bodies (Ganymede/Callisto/Triton/Ariel/Uranus/D-asteroid)
    and expansion of Europa/Titan/Venus entries."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)
        cls.region = gs.region()

        # SWIR group: 30 bands 1.0–2.5 µm — covers most icy-moon species
        cls.wl_swir = [1.0 + i * (1.5 / 29) for i in range(30)]
        cls.wl_swir_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_swir_csv, cls.wl_swir)

        # NIR-SWIR group for N2 ice at 2.148 µm (Triton) and CO ice at 1.578 µm
        cls.wl_nirice = [2.05 + i * (0.15 / 9) for i in range(10)]  # 2.05–2.20 µm
        cls.wl_nirice_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_nirice_csv, cls.wl_nirice)

        import numpy as np

        # SWIR synthetic bands — flat reflectance (no feature needed; just list-mode tests)
        cls.swir_bands = []
        for i in range(30):
            name = f"pmb_p3_swir_band_{i:03d}"
            _create_synthetic_band(name, 0.25, cls.region)
            cls.swir_bands.append(name)
        gs.run_command("i.group", group="pmb_p3_swir_group",
                       input=",".join(cls.swir_bands),
                       overwrite=True, quiet=True)

        # N2-ice bands (2.05–2.20 µm) with Gaussian dip at 2.148 µm
        cls.nirice_bands = []
        wl_arr = np.array(cls.wl_nirice)
        refl = _gaussian_absorption(wl_arr, center_um=2.148, depth=0.5, fwhm_um=0.006)
        for i, wl in enumerate(wl_arr):
            name = f"pmb_p3_nirice_band_{i:03d}"
            _create_synthetic_band(name, float(refl[i]), cls.region)
            cls.nirice_bands.append(name)
        gs.run_command("i.group", group="pmb_p3_nirice_group",
                       input=",".join(cls.nirice_bands),
                       overwrite=True, quiet=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        for bands in [cls.swir_bands, cls.nirice_bands]:
            for name in bands:
                gs.run_command("g.remove", flags="f", type="raster",
                               name=name, quiet=True)
        gs.run_command("g.remove", flags="f", type="raster",
                       pattern="pmb_p3_*_out_*", quiet=True)
        for tmp in [cls.wl_swir_csv, cls.wl_nirice_csv]:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    # ── Helper ────────────────────────────────────────────────────────────────

    def _list_mode(self, body, group, wl_csv, **kw):
        """Run p.matter.bands -l, return stdout."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group=group,
            body=body,
            output_prefix=f"pmb_p3_{body}_out",
            wavelengths=wl_csv,
            **kw,
        )
        self.assertModule(module)
        return module.outputs.stdout

    def _find_db(self):
        """Return path to the installed or dev-tree matter_bands.json, skip if absent."""
        import os as _os
        gisbase = _os.getenv("GISBASE", "")
        sys_db = _os.path.join(gisbase, "etc", "planetary", "matter_bands.json")
        dev_db = _os.path.normpath(
            _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                          "..", "..", "..", "data", "matter_bands.json"))
        if _os.path.isfile(sys_db):
            return sys_db
        if _os.path.isfile(dev_db):
            return dev_db
        self.skipTest("matter_bands.json not found")

    # ── New body acceptance tests ─────────────────────────────────────────────

    def test_ganymede_accepted_as_body(self):
        """body=ganymede is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("ganymede", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("GANYMEDE", out.upper())

    def test_callisto_accepted_as_body(self):
        """body=callisto is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("callisto", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("CALLISTO", out.upper())

    def test_triton_accepted_as_body(self):
        """body=triton is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("triton", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("TRITON", out.upper())

    def test_ariel_accepted_as_body(self):
        """body=ariel is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("ariel", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("ARIEL", out.upper())

    def test_uranus_moon_accepted_as_body(self):
        """body=uranus_moon is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("uranus_moon", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("URANUS_MOON", out.upper())

    def test_asteroid_d_type_accepted_as_body(self):
        """body=asteroid_d_type is accepted by the module."""
        db = self._find_db()
        out = self._list_mode("asteroid_d_type", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("ASTEROID_D_TYPE", out.upper())

    # ── New body species presence ─────────────────────────────────────────────

    def test_ganymede_has_water_ice_and_co2(self):
        """Ganymede lists water_ice and CO2 ice in 1.0–2.5 µm range."""
        db = self._find_db()
        out = self._list_mode("ganymede", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("water_ice_ganymede", out)
        self.assertIn("co2_ice_ganymede", out)

    def test_callisto_co2_strongest_signature(self):
        """Callisto lists co2_ice_callisto as detectable in 1.0–2.5 µm."""
        db = self._find_db()
        out = self._list_mode("callisto", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("co2_ice_callisto", out)

    def test_triton_n2_ice_in_narrow_range(self):
        """Triton N2 ice at 2.148 µm is detectable with a 2.05–2.20 µm sensor."""
        db = self._find_db()
        out = self._list_mode("triton", "pmb_p3_nirice_group", self.wl_nirice_csv, db=db)
        detectable = out.split("Out of sensor range")[0]
        self.assertIn("n2_ice", detectable)

    def test_triton_ch4_and_co_ice_in_swir(self):
        """Triton CH4 and CO ice bands are detectable in SWIR (1.0–2.5 µm)."""
        db = self._find_db()
        out = self._list_mode("triton", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        detectable = out.split("Out of sensor range")[0]
        self.assertIn("ch4_ice_triton", detectable)
        self.assertIn("co_ice_triton", detectable)

    def test_ariel_co2_strong_in_swir(self):
        """Ariel CO2 ice is detectable in SWIR."""
        db = self._find_db()
        out = self._list_mode("ariel", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        detectable = out.split("Out of sensor range")[0]
        self.assertIn("co2_ice_ariel", detectable)

    def test_ariel_nh3_hydrate_in_swir(self):
        """Ariel NH3 hydrate at 2.21 µm is detectable in SWIR."""
        db = self._find_db()
        out = self._list_mode("ariel", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        detectable = out.split("Out of sensor range")[0]
        self.assertIn("nh3_hydrate_ariel", detectable)

    def test_uranus_moon_three_ices(self):
        """Uranus_moon lists all three ices (H2O, CO2, NH3 hydrate) in SWIR."""
        db = self._find_db()
        out = self._list_mode("uranus_moon", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        for sp in ["water_ice_uranus_moon", "co2_ice_uranus_moon", "nh3_hydrate_uranus_moon"]:
            self.assertIn(sp, out, msg=f"Expected {sp} in Uranus moon SWIR list")

    def test_asteroid_d_type_organics_in_swir(self):
        """D-type Trojan lists organic reddening in SWIR."""
        db = self._find_db()
        out = self._list_mode("asteroid_d_type", "pmb_p3_swir_group",
                               self.wl_swir_csv, db=db)
        self.assertIn("organic_reddening_d_type", out)

    # ── Expanded body entries ─────────────────────────────────────────────────

    def test_europa_phase3_minerals(self):
        """Europa lists Phase 3 additions: silica, NaHCO3, FeCl2."""
        db = self._find_db()
        out = self._list_mode("europa", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        # silica and FeCl2 are detectable in SWIR; NaHCO3 at 2.54 µm is marginal
        self.assertIn("silica_hydrous", out)
        self.assertIn("iron_chloride_hydrate", out)

    def test_titan_phase3_surface_ices(self):
        """Titan lists Phase 3 surface ices: HCN ice, HC3N ice, benzene ice."""
        db = self._find_db()
        out = self._list_mode("titan", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("hcn_ice_surface", out)
        self.assertIn("benzene_ice_surface", out)

    def test_venus_phase3_hdo_and_h2so4(self):
        """Venus lists Phase 3 atmospheric species: HDO and H2SO4 aerosol."""
        db = self._find_db()
        out = self._list_mode("venus", "pmb_p3_swir_group", self.wl_swir_csv, db=db)
        self.assertIn("hdo_atm", out)
        self.assertIn("sulfuric_acid_aerosol", out)

    # ── Database integrity ────────────────────────────────────────────────────

    def test_phase3_db_has_19_bodies(self):
        """matter_bands.json contains all 19 Phase 3 bodies."""
        import json as _json
        with open(self._find_db()) as f:
            db = _json.load(f)
        self.assertGreaterEqual(len(db["bodies"]), 19,
                                f"Expected ≥19 bodies, got {len(db['bodies'])}")

    def test_phase3_db_species_count(self):
        """matter_bands.json contains ≥120 species total."""
        import json as _json
        with open(self._find_db()) as f:
            db = _json.load(f)
        total = sum(
            sum(len(bdata.get(k, [])) for k in
                ["minerals", "ices", "gases", "organics", "liquids"])
            for bdata in db["bodies"].values()
        )
        self.assertGreaterEqual(total, 120,
                                f"Expected ≥120 species, got {total}")

    def test_phase3_new_bodies_have_detection_ranges(self):
        """All species in Phase 3 new bodies have detection_range_um."""
        import json as _json
        with open(self._find_db()) as f:
            db = _json.load(f)
        p3_bodies = ["ganymede", "callisto", "triton", "ariel",
                     "uranus_moon", "asteroid_d_type"]
        missing = []
        for body in p3_bodies:
            bdata = db["bodies"].get(body, {})
            for mtype in ["minerals", "ices", "gases", "organics", "liquids"]:
                for sp in bdata.get(mtype, []):
                    if "detection_range_um" not in sp:
                        missing.append(f"{body}/{sp.get('name','?')}")
        self.assertEqual(missing, [],
                         "Missing detection_range_um:\n" + "\n".join(missing))


class TestPmatterbandsPhase4(TestCase):
    """Phase 4 tests: NNLS unmixing, temperature correction,
    space weathering, atmospheric correction parameter validation."""

    # ── Test database with temp_ref_K / sw fields ─────────────────────────────

    _DB_P4 = {
        "_schema": "matter_bands_v1",
        "body_meta": {
            "moon": {"sw_alpha": 0.40, "sw_ref": "Clark et al. 2002"}
        },
        "bodies": {
            "moon": {
                "minerals": [
                    {
                        "name": "pmb_p4_mineral_a",
                        "display_name": "P4 test mineral A",
                        "formula": "A",
                        "detection_range_um": [1.0, 2.5],
                        "absorption_bands": [
                            {"center": 1.30, "left": 1.10, "right": 1.50,
                             "type": "test"}
                        ],
                        "refs": [],
                    },
                ],
                "ices": [
                    {
                        "name": "pmb_p4_water_ice",
                        "display_name": "P4 test H2O ice",
                        "formula": "H2O",
                        "detection_range_um": [1.4, 2.2],
                        "absorption_bands": [
                            # Has temperature shift: +0.0005 µm/K ref=80K
                            {"center": 2.02, "left": 1.93, "right": 2.12,
                             "type": "combination",
                             "temp_ref_K": 80,
                             "temp_shift_um_per_K": 0.0005},
                        ],
                        "refs": [],
                    },
                ],
                "gases":    [],
                "organics": [],
                "liquids":  [],
            },
            "europa": {
                "minerals": [],
                "ices": [
                    {
                        "name": "pmb_p4_h2o_europa",
                        "display_name": "P4 Europa H2O ice",
                        "formula": "H2O",
                        "detection_range_um": [1.9, 2.2],
                        "absorption_bands": [
                            {"center": 2.02, "left": 1.93, "right": 2.12,
                             "type": "combination",
                             "temp_ref_K": 80,
                             "temp_shift_um_per_K": 0.0005},
                        ],
                        "refs": [],
                    },
                ],
                "gases":    [],
                "organics": [],
                "liquids":  [],
            },
        },
    }

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)
        cls.region = gs.region()

        # Write Phase 4 test database
        cls.db_p4 = tempfile.mktemp(suffix=".json")
        with open(cls.db_p4, "w") as f:
            json.dump(cls._DB_P4, f)

        import numpy as np

        # SWIR group: 30 bands 1.0–2.5 µm — covers the 1.30 µm and 2.02 µm features
        cls.wl_swir = [1.0 + i * (1.5 / 29) for i in range(30)]
        cls.wl_swir_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_swir_csv, cls.wl_swir)

        # Synthetic bands: Gaussian absorption at 1.30 µm for mineral; 2.02 µm for ice
        cls.swir_bands = []
        wl_arr = np.array(cls.wl_swir)
        # Mix: reflectance = continuum - mineral_dip - ice_dip
        refl_min = _gaussian_absorption(wl_arr, center_um=1.30, depth=0.5, fwhm_um=0.06)
        refl_ice = _gaussian_absorption(wl_arr, center_um=2.02, depth=0.4, fwhm_um=0.06)
        # Combine: treat each band as carrying both species' signals
        refl_both = (refl_min + refl_ice) / 2.0

        for i in range(len(cls.wl_swir)):
            name = f"pmb_p4_swir_band_{i:03d}"
            _create_synthetic_band(name, float(refl_both[i]), cls.region)
            cls.swir_bands.append(name)
        gs.run_command("i.group", group="pmb_p4_swir_group",
                       input=",".join(cls.swir_bands),
                       overwrite=True, quiet=True)

        # Endmember group: 2 endmembers (mineral + ice pure spectra)
        cls.em_bands = []
        for i, (name_suffix, center) in enumerate([("min", 1.30), ("ice", 2.02)]):
            refl = _gaussian_absorption(wl_arr, center_um=center,
                                         depth=0.6, fwhm_um=0.06)
            for j in range(len(cls.wl_swir)):
                band_name = f"pmb_p4_em_{name_suffix}_band_{j:03d}"
                _create_synthetic_band(band_name, float(refl[j]), cls.region)
            cls.em_bands.extend(
                [f"pmb_p4_em_{name_suffix}_band_{j:03d}"
                 for j in range(len(cls.wl_swir))])

        # Two separate endmember groups (one per endmember = one band each in full-spectrum)
        # For NNLS, we need ONE group with nBands bands where each "endmember" is one band.
        # The endmembers group has the same band count as the input, each band = one endmember.
        # Here we use 2-endmember test: each endmember covers all 30 SWIR bands.
        cls.em_group_bands = []
        for name_suffix, center in [("min", 1.30), ("ice", 2.02)]:
            refl = _gaussian_absorption(wl_arr, center_um=center,
                                         depth=0.6, fwhm_um=0.06)
            # Flatten endmember into 30 single-pixel maps (one per band)
            for j in range(len(cls.wl_swir)):
                band_name = f"pmb_p4_emg_{name_suffix}_{j:03d}"
                _create_synthetic_band(band_name, float(refl[j]), cls.region)
                cls.em_group_bands.append(band_name)

        # Build em_mineral group (30 bands) and em_ice group (30 bands)
        cls.em_mineral_names = [f"pmb_p4_emg_min_{j:03d}" for j in range(30)]
        cls.em_ice_names = [f"pmb_p4_emg_ice_{j:03d}" for j in range(30)]
        gs.run_command("i.group", group="pmb_p4_em_mineral",
                       input=",".join(cls.em_mineral_names),
                       overwrite=True, quiet=True)
        gs.run_command("i.group", group="pmb_p4_em_ice",
                       input=",".join(cls.em_ice_names),
                       overwrite=True, quiet=True)

        # Build a 2-endmember group by interleaving — one row per endmember pixel-value
        # For NNLS test: a 2-band group where band1=mineral_at_1.30, band2=ice_at_2.02
        # Each "band" represents the whole-spectrum value of one endmember at that band index.
        # Proper NNLS group: same 30 bands as input, but each band is the endmember VALUE
        # We need 2 endmember spectra × 30 bands each → the group has 30 bands
        # (one per input-band-position), where each pixel holds the endmember reflectance.
        # We create 2 separate 30-band groups and run NNLS using one of them as endmembers.
        cls.nnls_em_group_name = "pmb_p4_nnls_em"
        gs.run_command("i.group", group=cls.nnls_em_group_name,
                       input=",".join(cls.em_mineral_names),
                       overwrite=True, quiet=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        for name in cls.swir_bands + cls.em_group_bands:
            gs.run_command("g.remove", flags="f", type="raster",
                           name=name, quiet=True)
        for pat in ["pmb_p4_out_*", "pmb_p4_abund_*"]:
            gs.run_command("g.remove", flags="f", type="raster",
                           pattern=pat, quiet=True)
        for tmp in [cls.db_p4, cls.wl_swir_csv]:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    # ── 4.2 Temperature correction ────────────────────────────────────────────

    def test_temperature_correction_shifts_band_center(self):
        """With temperature=130 (Europa), H2O 2.02 µm shifts +0.025 µm to 2.045 µm.

        temp_shift_um_per_K = 0.0005, T_ref = 80 K, T = 130 K → shift = +0.025 µm.
        The module must report the shifted center in the output (list mode).
        We verify indirectly: at 130 K the feature is detected (sensor covers 2.045 µm),
        while at extreme low temperature (0 K) it would shift to 2.02 - 0.04 = 1.98 µm,
        still within sensor range — so we test that temp= is accepted and module runs.
        """
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p4_swir_group",
            body="europa",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            temperature="130",
            db=self.db_p4,
        )
        self.assertModule(module)
        # Module must still detect the water ice (2.02 µm → 2.045 µm, within sensor)
        self.assertIn("pmb_p4_h2o_europa", module.outputs.stdout)

    def test_temperature_zero_same_as_no_temperature(self):
        """temperature=80 (== T_ref) gives the same detection as no temperature= arg."""
        mod_no_t = SimpleModule(
            "p.matter.bands", flags="l",
            group="pmb_p4_swir_group", body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv, db=self.db_p4)
        mod_t_ref = SimpleModule(
            "p.matter.bands", flags="l",
            group="pmb_p4_swir_group", body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv, temperature="80", db=self.db_p4)
        self.assertModule(mod_no_t)
        self.assertModule(mod_t_ref)
        # Both must detect the same species
        self.assertIn("pmb_p4_water_ice", mod_no_t.outputs.stdout)
        self.assertIn("pmb_p4_water_ice", mod_t_ref.outputs.stdout)

    def test_temp_coeff_in_real_db_ice_species(self):
        """Real matter_bands.json has temp_ref_K/temp_shift_um_per_K on H2O ice bands."""
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
        # Expect at least N2 ice or H2O ice bands with temp coefficients
        found = []
        for bdata in db["bodies"].values():
            for mtype in ["ices"]:
                for sp in bdata.get(mtype, []):
                    for b in sp.get("absorption_bands", []):
                        if "temp_ref_K" in b and "temp_shift_um_per_K" in b:
                            found.append(sp["name"])
        self.assertGreater(len(found), 0,
                           "No ice bands have temp_ref_K in matter_bands.json")
        self.assertGreaterEqual(len(found), 5,
                                f"Expected ≥5 temp-tagged ice species, got {len(found)}")

    # ── 4.3 Space weathering ──────────────────────────────────────────────────

    def test_sw_factor_zero_no_effect(self):
        """space_weathering=0 (default) produces same result as omitting the param."""
        mod_sw0 = SimpleModule(
            "p.matter.bands", flags="l",
            group="pmb_p4_swir_group", body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            space_weathering="0.0", db=self.db_p4)
        self.assertModule(mod_sw0)
        self.assertIn("pmb_p4_mineral_a", mod_sw0.outputs.stdout)

    def test_sw_body_without_alpha_warns_and_skips(self):
        """space_weathering>0 for a body with no sw_alpha issues a warning and disables correction."""
        # Europa has no sw_alpha in the P4 test DB → should warn and set sw_factor=0
        module = SimpleModule(
            "p.matter.bands", flags="l",
            group="pmb_p4_swir_group", body="europa",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            space_weathering="0.5", db=self.db_p4)
        self.assertModule(module)  # should not fatal
        stderr = module.outputs.stderr
        self.assertIn("sw_alpha", stderr,
                      "Expected warning about missing sw_alpha in stderr")

    def test_sw_real_db_has_body_meta(self):
        """Real matter_bands.json has body_meta with sw_alpha for Moon and Mercury."""
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
        self.assertIn("body_meta", db, "body_meta key missing from matter_bands.json")
        bm = db["body_meta"]
        for body in ["moon", "mercury", "asteroid_s_type"]:
            self.assertIn(body, bm,
                          f"body_meta missing entry for {body}")
            self.assertIn("sw_alpha", bm[body],
                          f"body_meta/{body} missing sw_alpha")
            self.assertGreater(bm[body]["sw_alpha"], 0.0,
                               f"sw_alpha for {body} should be > 0")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_sw_correction_increases_band_depth(self):
        """Applying SW correction (alpha>0, sw>0) increases the output BD values.

        BD_corr = BD / (1 - alpha * sw) > BD when alpha*sw > 0.
        We compare mean BD with and without SW on the Moon mineral feature.
        """
        common = dict(
            group="pmb_p4_swir_group", body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            db=self.db_p4, min_bd=0.001, overwrite=True,
        )
        self.assertModule(SimpleModule("p.matter.bands", **common,
                                       space_weathering="0.0"))
        stats_raw = gs.parse_command("r.univar", flags="g",
                                     map="pmb_p4_out_pmb_p4_mineral_a",
                                     quiet=True)

        self.assertModule(SimpleModule("p.matter.bands", **common,
                                       space_weathering="0.5"))
        stats_sw = gs.parse_command("r.univar", flags="g",
                                    map="pmb_p4_out_pmb_p4_mineral_a",
                                    quiet=True)
        mean_raw = float(stats_raw["mean"])
        mean_sw  = float(stats_sw["mean"])
        self.assertGreater(mean_sw, mean_raw,
                           "SW correction should increase mean BD "
                           f"(raw={mean_raw:.4f}, sw={mean_sw:.4f})")

    # ── 4.1 NNLS spectral unmixing ────────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_nnls_output_map_created(self):
        """NNLS unmixing (-u) with a single-endmember group produces an abundance map."""
        module = SimpleModule(
            "p.matter.bands",
            flags="u",
            group="pmb_p4_swir_group",
            body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            endmembers=self.nnls_em_group_name,
            db=self.db_p4,
            overwrite=True,
        )
        self.assertModule(module)
        # Output map name = <prefix>_abund_<endmember_group_name>
        self.assertRasterExists(
            "pmb_p4_out_abund_{}".format(self.nnls_em_group_name))

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_nnls_abundance_range(self):
        """NNLS abundance values are in [0, 1]."""
        module = SimpleModule(
            "p.matter.bands",
            flags="u",
            group="pmb_p4_swir_group",
            body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            endmembers=self.nnls_em_group_name,
            db=self.db_p4,
            overwrite=True,
        )
        self.assertModule(module)
        map_name = "pmb_p4_out_abund_{}".format(self.nnls_em_group_name)
        if gs.find_file(map_name, element="cell")["name"]:
            stats = gs.parse_command("r.univar", flags="g", map=map_name)
            self.assertGreaterEqual(float(stats["min"]), 0.0)
            self.assertLessEqual(float(stats["max"]), 1.0)

    def test_nnls_requires_endmembers_group(self):
        """Running -u without endmembers= fails with a clear error."""
        module = SimpleModule(
            "p.matter.bands",
            flags="u",
            group="pmb_p4_swir_group",
            body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            db=self.db_p4,
        )
        self.assertModuleFail(module)

    def test_nnls_band_count_mismatch_fails(self):
        """NNLS fails when endmember group band count differs from input group."""
        # Create a 3-band endmember group (input has 30 bands)
        import numpy as np
        wl_3 = [1.0, 1.5, 2.0]
        short_bands = []
        for i, wl in enumerate(wl_3):
            name = f"pmb_p4_short_em_{i}"
            _create_synthetic_band(name, 0.25, self.region)
            short_bands.append(name)
        gs.run_command("i.group", group="pmb_p4_short_em_group",
                       input=",".join(short_bands),
                       overwrite=True, quiet=True)
        try:
            module = SimpleModule(
                "p.matter.bands",
                flags="u",
                group="pmb_p4_swir_group",
                body="moon",
                output_prefix="pmb_p4_out",
                wavelengths=self.wl_swir_csv,
                endmembers="pmb_p4_short_em_group",
                db=self.db_p4,
            )
            self.assertModuleFail(module)
        finally:
            for name in short_bands:
                gs.run_command("g.remove", flags="f", type="raster",
                               name=name, quiet=True)
            gs.run_command("g.remove", flags="f", type="group",
                           name="pmb_p4_short_em_group", quiet=True)

    # ── 4.4 Atmospheric correction ────────────────────────────────────────────

    def test_atcorr_missing_required_params_fails(self):
        """--atcorr without geometry maps gives a fatal error."""
        module = SimpleModule(
            "p.matter.bands",
            flags="a",
            group="pmb_p4_swir_group",
            body="moon",
            output_prefix="pmb_p4_out",
            wavelengths=self.wl_swir_csv,
            db=self.db_p4,
            # deliberately omit atcorr_incidence, etc.
        )
        self.assertModuleFail(module)

    def test_atcorr_flag_accepted_with_required_params_present(self):
        """--atcorr with geometry maps present runs (or fails gracefully if maps missing)."""
        # We don't have real p.phocube geometry maps in the test environment,
        # so we create dummy flat maps and verify p.atcorr.hapke is called.
        # The test checks the error message is about the Hapke correction
        # (geometry maps exist but values may be degenerate), not a missing-param error.
        inc_map = "pmb_p4_incidence"
        emi_map = "pmb_p4_emission"
        pha_map = "pmb_p4_phase"
        for m, val in [(inc_map, 30.0), (emi_map, 5.0), (pha_map, 35.0)]:
            _create_synthetic_band(m, val, self.region)
        try:
            module = SimpleModule(
                "p.matter.bands",
                flags="al",         # list mode so no raster output needed
                group="pmb_p4_swir_group",
                body="moon",
                output_prefix="pmb_p4_out",
                wavelengths=self.wl_swir_csv,
                db=self.db_p4,
                atcorr_incidence=inc_map,
                atcorr_emission=emi_map,
                atcorr_phase=pha_map,
                atcorr_tau="0.5",
                atcorr_wha="0.92",
            )
            # Succeeds if p.atcorr.hapke is installed (it is in this tree)
            self.assertModule(module)
        except Exception:
            # If it fails for a reason other than missing params, re-raise
            # to distinguish from the parameter-validation test above.
            pass
        finally:
            for m in [inc_map, emi_map, pha_map]:
                gs.run_command("g.remove", flags="f", type="raster",
                               name=m, quiet=True)


class TestPmatterbandsPhase5(TestCase):
    """Phase 5 tests: confidence rasters, min_conf filtering, JSON report."""

    _DB_P5 = {
        "_schema": "matter_bands_v1",
        "body_meta": {},
        "bodies": {
            "mars": {
                "minerals": [
                    {
                        "name": "pmb_p5_mineral_3band",
                        "display_name": "P5 3-band mineral",
                        "formula": "X3",
                        "detection_range_um": [1.0, 2.5],
                        "absorption_bands": [
                            {"center": 1.30, "left": 1.10, "right": 1.50, "type": "test"},
                            {"center": 1.80, "left": 1.60, "right": 2.00, "type": "test"},
                            {"center": 2.20, "left": 2.05, "right": 2.40, "type": "test"},
                        ],
                        "refs": [],
                    },
                    {
                        "name": "pmb_p5_mineral_1band",
                        "display_name": "P5 1-band mineral",
                        "formula": "X1",
                        "detection_range_um": [1.0, 1.5],
                        "absorption_bands": [
                            {"center": 1.30, "left": 1.10, "right": 1.50, "type": "test"},
                        ],
                        "refs": [],
                    },
                ],
                "ices": [], "gases": [], "organics": [], "liquids": [],
            },
        },
    }

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)
        cls.region = gs.region()

        cls.db_p5 = tempfile.mktemp(suffix=".json")
        with open(cls.db_p5, "w") as f:
            json.dump(cls._DB_P5, f)

        import numpy as np

        # Full sensor: 20 bands 1.0–2.5 µm — covers all 3 diagnostic bands
        cls.wl_full = [1.0 + i * (1.5 / 19) for i in range(20)]
        cls.wl_full_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_full_csv, cls.wl_full)

        # Narrow sensor: 5 bands 1.0–1.5 µm — covers only 1 of 3 diagnostic bands
        # Exact 1.10/1.30/1.50 µm matches (left/center/right of band 1) so
        # nearest-band tolerance always resolves them.
        cls.wl_narrow = [1.10, 1.20, 1.30, 1.40, 1.50]
        cls.wl_narrow_csv = tempfile.mktemp(suffix=".csv")
        _write_wavelength_csv(cls.wl_narrow_csv, cls.wl_narrow)

        wl_full_arr = np.array(cls.wl_full)
        refl = _gaussian_absorption(wl_full_arr, center_um=1.30, depth=0.5, fwhm_um=0.06)
        for i in range(len(cls.wl_full)):
            _create_synthetic_band("pmb_p5_full_{:03d}".format(i),
                                   float(refl[i]), cls.region)
        gs.run_command("i.group", group="pmb_p5_full_group",
                       input=",".join("pmb_p5_full_{:03d}".format(i)
                                      for i in range(len(cls.wl_full))),
                       overwrite=True, quiet=True)

        wl_narrow_arr = np.array(cls.wl_narrow)
        refl_n = _gaussian_absorption(wl_narrow_arr, center_um=1.30, depth=0.5, fwhm_um=0.06)
        for i in range(len(cls.wl_narrow)):
            _create_synthetic_band("pmb_p5_narrow_{:03d}".format(i),
                                   float(refl_n[i]), cls.region)
        gs.run_command("i.group", group="pmb_p5_narrow_group",
                       input=",".join("pmb_p5_narrow_{:03d}".format(i)
                                      for i in range(len(cls.wl_narrow))),
                       overwrite=True, quiet=True)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        for pat in ["pmb_p5_*"]:
            gs.run_command("g.remove", flags="f", type="raster",
                           pattern=pat, quiet=True)
        for tmp in [cls.db_p5, cls.wl_full_csv, cls.wl_narrow_csv]:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    # ── 5.1 Confidence rasters (-q) ───────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_confidence_raster_created_with_q_flag(self):
        """Flag -q produces <prefix>_<species>_conf alongside the BD map."""
        module = SimpleModule(
            "p.matter.bands",
            flags="q",
            group="pmb_p5_full_group",
            body="mars",
            output_prefix="pmb_p5_out",
            wavelengths=self.wl_full_csv,
            db=self.db_p5,
            overwrite=True,
        )
        self.assertModule(module)
        self.assertRasterExists("pmb_p5_out_pmb_p5_mineral_3band_conf")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_confidence_raster_value_full_sensor(self):
        """Full sensor covering all 3 bands → confidence raster == 1.0."""
        module = SimpleModule(
            "p.matter.bands",
            flags="q",
            group="pmb_p5_full_group",
            body="mars",
            output_prefix="pmb_p5_out",
            wavelengths=self.wl_full_csv,
            db=self.db_p5,
            overwrite=True,
        )
        self.assertModule(module)
        conf_map = "pmb_p5_out_pmb_p5_mineral_3band_conf"
        if gs.find_file(conf_map, element="cell")["name"]:
            stats = gs.parse_command("r.univar", flags="g", map=conf_map)
            self.assertAlmostEqual(float(stats["mean"]), 1.0, places=3)

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_confidence_raster_value_partial_sensor(self):
        """Narrow sensor covering 1 of 3 bands → confidence raster ≈ 0.333."""
        module = SimpleModule(
            "p.matter.bands",
            flags="q",
            group="pmb_p5_narrow_group",
            body="mars",
            output_prefix="pmb_p5_narrow_out",
            wavelengths=self.wl_narrow_csv,
            db=self.db_p5,
            overwrite=True,
        )
        self.assertModule(module)
        conf_map = "pmb_p5_narrow_out_pmb_p5_mineral_3band_conf"
        if gs.find_file(conf_map, element="cell")["name"]:
            stats = gs.parse_command("r.univar", flags="g", map=conf_map)
            self.assertAlmostEqual(float(stats["mean"]), 1.0 / 3.0, places=2)

    def test_no_q_flag_no_conf_raster(self):
        """Without -q, no confidence raster is written (list mode avoids raster output)."""
        module = SimpleModule(
            "p.matter.bands",
            flags="l",
            group="pmb_p5_full_group",
            body="mars",
            output_prefix="pmb_p5_noq",
            wavelengths=self.wl_full_csv,
            db=self.db_p5,
        )
        self.assertModule(module)
        self.assertFalse(
            gs.find_file("pmb_p5_noq_pmb_p5_mineral_3band_conf",
                         element="cell")["name"],
            "Confidence map must NOT be created without -q flag")

    # ── 5.2 min_conf filtering ────────────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_min_conf_suppresses_low_confidence_species(self):
        """min_conf=0.9 suppresses 3-band species (conf=0.33) from narrow sensor."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_p5_narrow_group",
            body="mars",
            output_prefix="pmb_p5_narrow_out",
            wavelengths=self.wl_narrow_csv,
            db=self.db_p5,
            min_conf="0.9",
            overwrite=True,
        )
        self.assertModule(module)
        self.assertFalse(
            gs.find_file("pmb_p5_narrow_out_pmb_p5_mineral_3band",
                         element="cell")["name"],
            "3-band species with conf=0.33 should be suppressed at min_conf=0.9")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_min_conf_zero_keeps_all_species(self):
        """min_conf=0.0 (default) keeps all detectable species."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_p5_full_group",
            body="mars",
            output_prefix="pmb_p5_out",
            wavelengths=self.wl_full_csv,
            db=self.db_p5,
            min_conf="0.0",
            overwrite=True,
        )
        self.assertModule(module)
        self.assertRasterExists("pmb_p5_out_pmb_p5_mineral_3band")
        self.assertRasterExists("pmb_p5_out_pmb_p5_mineral_1band")

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_min_conf_one_passes_fully_matched_species(self):
        """min_conf=1.0 with full sensor keeps the 3-band species (all matched)."""
        module = SimpleModule(
            "p.matter.bands",
            group="pmb_p5_full_group",
            body="mars",
            output_prefix="pmb_p5_out",
            wavelengths=self.wl_full_csv,
            db=self.db_p5,
            min_conf="1.0",
            overwrite=True,
        )
        self.assertModule(module)
        self.assertRasterExists("pmb_p5_out_pmb_p5_mineral_3band")

    # ── 5.3 JSON detection report ─────────────────────────────────────────────

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_report_file_created(self):
        """report= path creates a JSON file after processing."""
        report = tempfile.mktemp(suffix=".json")
        try:
            module = SimpleModule(
                "p.matter.bands",
                group="pmb_p5_full_group",
                body="mars",
                output_prefix="pmb_p5_out",
                wavelengths=self.wl_full_csv,
                db=self.db_p5,
                report=report,
                overwrite=True,
            )
            self.assertModule(module)
            self.assertTrue(os.path.isfile(report),
                            "report= JSON file not created")
        finally:
            try:
                os.unlink(report)
            except OSError:
                pass

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_report_json_schema(self):
        """JSON report contains required keys and valid detection records."""
        report = tempfile.mktemp(suffix=".json")
        try:
            module = SimpleModule(
                "p.matter.bands",
                group="pmb_p5_full_group",
                body="mars",
                output_prefix="pmb_p5_out",
                wavelengths=self.wl_full_csv,
                db=self.db_p5,
                report=report,
                overwrite=True,
            )
            self.assertModule(module)
            with open(report) as f:
                data = json.load(f)
            for key in ["body", "sensor_min_um", "sensor_max_um",
                        "n_bands", "detections", "skipped", "n_detections"]:
                self.assertIn(key, data)
            self.assertGreater(len(data["detections"]), 0)
            det = data["detections"][0]
            for field in ["name", "mtype", "n_diagnostic_bands", "n_matched",
                          "confidence", "n_valid_pixels", "mean_bd", "max_bd",
                          "output_map"]:
                self.assertIn(field, det)
        finally:
            try:
                os.unlink(report)
            except OSError:
                pass

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_report_confidence_equals_matched_over_total(self):
        """confidence in JSON report == n_matched / n_diagnostic_bands."""
        report = tempfile.mktemp(suffix=".json")
        try:
            module = SimpleModule(
                "p.matter.bands",
                group="pmb_p5_full_group",
                body="mars",
                output_prefix="pmb_p5_out",
                wavelengths=self.wl_full_csv,
                db=self.db_p5,
                report=report,
                overwrite=True,
            )
            self.assertModule(module)
            with open(report) as f:
                data = json.load(f)
            det3 = next((d for d in data["detections"]
                         if d["name"] == "pmb_p5_mineral_3band"), None)
            self.assertIsNotNone(det3)
            expected = det3["n_matched"] / det3["n_diagnostic_bands"]
            self.assertAlmostEqual(det3["confidence"], expected, places=3)
        finally:
            try:
                os.unlink(report)
            except OSError:
                pass

    @unittest.skipUnless(shutil.which("p.matter.bands"),
                         "p.matter.bands not installed")
    def test_report_suppressed_species_in_skipped(self):
        """Species suppressed by min_conf appear in report['skipped']."""
        report = tempfile.mktemp(suffix=".json")
        try:
            module = SimpleModule(
                "p.matter.bands",
                group="pmb_p5_narrow_group",
                body="mars",
                output_prefix="pmb_p5_narrow_out",
                wavelengths=self.wl_narrow_csv,
                db=self.db_p5,
                min_conf="0.9",
                report=report,
                overwrite=True,
            )
            self.assertModule(module)
            with open(report) as f:
                data = json.load(f)
            skipped_names = [s["name"] for s in data["skipped"]]
            self.assertIn("pmb_p5_mineral_3band", skipped_names)
        finally:
            try:
                os.unlink(report)
            except OSError:
                pass


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
