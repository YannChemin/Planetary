"""
Testsuite for p.in.rings.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.rings/testsuite/test_p_in_rings.py -v

Interface tests run unconditionally.  The full ring-projection tests (which
require SPICE kernels) are skipped unless the kernel paths referenced in the
Cassini SOI B-ring example are present under $HOME/RSDATA.

When SPICE kernels ARE available the tests:
  1. Create a synthetic 4×4 raster as a stand-in for the raw camera image.
  2. Exercise both projection= modes (radlong and polar).
  3. Verify the output raster and planetary.json sidecar for each mode.
"""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from p_meta import METADATA_FILENAME, SCHEMA_VERSION


# ---------------------------------------------------------------------------
# SPICE kernel availability check
# ---------------------------------------------------------------------------

_RSDATA = Path(os.path.expanduser("~")) / "RSDATA"

_CASSINI_KERNELS = [
    _RSDATA / "SPICE" / "cassini" / "kernels" / "lsk" / "naif0012.tls",
    _RSDATA / "SPICE" / "cassini" / "kernels" / "spk" / "180428R_SCPSE_04154_04190.bsp",
]

HAS_SPICE_KERNELS = all(k.exists() for k in _CASSINI_KERNELS)


def _cell_misc_path(mapname: str) -> Path:
    env = gs.gisenv()
    return (
        Path(env["GISDBASE"]) / env["LOCATION_NAME"] / env["MAPSET"]
        / "cell_misc" / mapname / METADATA_FILENAME
    )


def _skip_if_no_spice(test_instance):
    import unittest
    if not HAS_SPICE_KERNELS:
        raise unittest.SkipTest(
            "SPICE kernels not found under $HOME/RSDATA — "
            "skipping ring-projection test"
        )


# ---------------------------------------------------------------------------
# Interface tests (always run)
# ---------------------------------------------------------------------------

class TestPInRingsInterface(TestCase):
    """Module interface tests that do not require SPICE kernels."""

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.rings"),
                             "p.in.rings not found on PATH")

    def test_interface_description(self):
        rc = subprocess.run(
            ["p.in.rings", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode
        self.assertEqual(rc, 0)

    def test_missing_required_args_fail(self):
        """Running without required time= and instrument= must fail."""
        from grass.gunittest.gmodules import SimpleModule
        module = SimpleModule("p.in.rings",
                              input="__no_such_map__",
                              output="__no_such_out__")
        self.assertModuleFail(module)

    def test_projection_option_in_interface(self):
        """projection= option must appear in --interface-description output."""
        result = subprocess.run(
            ["p.in.rings", "--interface-description"],
            capture_output=True, text=True,
        )
        self.assertIn("projection", result.stdout,
                      "projection= option missing from interface description")

    def test_polar_in_projection_choices(self):
        """'polar' must be a listed choice for projection=."""
        result = subprocess.run(
            ["p.in.rings", "--interface-description"],
            capture_output=True, text=True,
        )
        self.assertIn("polar", result.stdout)

    def test_radlong_in_projection_choices(self):
        """'radlong' must be a listed choice for projection=."""
        result = subprocess.run(
            ["p.in.rings", "--interface-description"],
            capture_output=True, text=True,
        )
        self.assertIn("radlong", result.stdout)


# ---------------------------------------------------------------------------
# SPICE-gated: radlong projection (default, current behaviour)
# ---------------------------------------------------------------------------

class TestPInRingsRadlong(TestCase):
    """radlong projection + planetary.json; skipped without SPICE kernels."""

    raw_input = "test_rings_raw_rl"
    out = "test_rings_radlong"
    _INSTRUMENT = -82360
    _TIME = "2004-07-01T03:11:40"
    _SPACECRAFT = "CASSINI"
    _BODY = "SATURN"
    _FRAME = "IAU_SATURN"

    @classmethod
    def setUpClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        cls.use_temp_region()
        gs.run_command("g.region", rows=4, cols=4, n=4, s=0, e=4, w=0, res=1)
        gs.mapcalc(
            f"{cls.raw_input} = (row() - 1) * 4 + (col() - 1)",
            overwrite=True,
        )
        # radlong region: N/S=ring_radius[km], E/W=ring_longitude[deg]
        gs.run_command("g.region", n=120000, s=90000, e=10, w=0, rows=8, cols=8)

    @classmethod
    def tearDownClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.raw_input},{cls.out}",
                       flags="f", quiet=True)
        cls.del_temp_region()

    def _kernel_list(self):
        return ",".join(str(k) for k in _CASSINI_KERNELS)

    def test_radlong_creates_raster(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="radlong", overwrite=True,
        )
        self.assertRasterExists(self.out)

    def test_radlong_planetary_json_created(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="radlong", overwrite=True,
        )
        self.assertTrue(_cell_misc_path(self.out).exists())

    def test_radlong_planetary_json_content(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="radlong", overwrite=True,
        )
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)

        self.assertEqual(data["schema_version"], SCHEMA_VERSION)
        self.assertEqual(data["data_type"], "rings")
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertEqual(planetary.get("body"), self._BODY)
        self.assertEqual(planetary.get("mission"), self._SPACECRAFT)
        self.assertIn(self._TIME, data.get("acquisition_datetime", ""))
        # projection mode must be recorded
        self.assertEqual(planetary.get("projection"), "radlong")


# ---------------------------------------------------------------------------
# SPICE-gated: polar projection (new mode)
# ---------------------------------------------------------------------------

class TestPInRingsPolar(TestCase):
    """polar projection + planetary.json; skipped without SPICE kernels."""

    raw_input = "test_rings_raw_pol"
    out = "test_rings_polar"
    _INSTRUMENT = -82360
    _TIME = "2004-07-01T03:11:40"
    _SPACECRAFT = "CASSINI"
    _BODY = "SATURN"
    _FRAME = "IAU_SATURN"

    @classmethod
    def setUpClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        cls.use_temp_region()
        gs.run_command("g.region", rows=4, cols=4, n=4, s=0, e=4, w=0, res=1)
        gs.mapcalc(
            f"{cls.raw_input} = (row() - 1) * 4 + (col() - 1)",
            overwrite=True,
        )
        # polar region: both axes in ring-plane km (isotropic).
        # Box centred near the SOI ring patch at ~(34400, 79200) km.
        gs.run_command(
            "g.region",
            n=82000, s=76000, e=38000, w=30000,
            rows=8, cols=8,
        )

    @classmethod
    def tearDownClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        gs.run_command("g.remove", type="raster",
                       name=f"{cls.raw_input},{cls.out}",
                       flags="f", quiet=True)
        cls.del_temp_region()

    def _kernel_list(self):
        return ",".join(str(k) for k in _CASSINI_KERNELS)

    def test_polar_creates_raster(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="polar", overwrite=True,
        )
        self.assertRasterExists(self.out)

    def test_polar_planetary_json_created(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="polar", overwrite=True,
        )
        self.assertTrue(_cell_misc_path(self.out).exists())

    def test_polar_planetary_json_projection_field(self):
        """planetary.json must record projection=polar."""
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="polar", overwrite=True,
        )
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertEqual(planetary.get("projection"), "polar",
                         "projection field missing or wrong in planetary.json")

    def test_polar_planetary_json_body_mission(self):
        _skip_if_no_spice(self)
        self.assertModule(
            "p.in.rings",
            input=self.raw_input, output=self.out,
            time=self._TIME, instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT, body=self._BODY,
            frame=self._FRAME, kernels=self._kernel_list(),
            projection="polar", overwrite=True,
        )
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)
        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertEqual(planetary.get("body"), self._BODY)
        self.assertEqual(planetary.get("mission"), self._SPACECRAFT)

    def test_polar_and_radlong_same_source_different_json(self):
        """radlong and polar runs on the same image produce distinct outputs
        with the correct projection field in each planetary.json."""
        _skip_if_no_spice(self)
        out_rl = "test_rings_cmp_radlong"
        out_pol = "test_rings_cmp_polar"
        try:
            # radlong region
            gs.run_command("g.region",
                           n=120000, s=90000, e=10, w=0, rows=8, cols=8)
            self.assertModule(
                "p.in.rings",
                input=self.raw_input, output=out_rl,
                time=self._TIME, instrument=self._INSTRUMENT,
                spacecraft=self._SPACECRAFT, body=self._BODY,
                frame=self._FRAME, kernels=self._kernel_list(),
                projection="radlong", overwrite=True,
            )
            # polar region
            gs.run_command("g.region",
                           n=82000, s=76000, e=38000, w=30000,
                           rows=8, cols=8)
            self.assertModule(
                "p.in.rings",
                input=self.raw_input, output=out_pol,
                time=self._TIME, instrument=self._INSTRUMENT,
                spacecraft=self._SPACECRAFT, body=self._BODY,
                frame=self._FRAME, kernels=self._kernel_list(),
                projection="polar", overwrite=True,
            )

            with open(_cell_misc_path(out_rl)) as fh:
                d_rl = json.load(fh)
            with open(_cell_misc_path(out_pol)) as fh:
                d_pol = json.load(fh)

            p_rl  = d_rl.get("extended_metadata",  {}).get("planetary", {})
            p_pol = d_pol.get("extended_metadata", {}).get("planetary", {})
            self.assertEqual(p_rl.get("projection"),  "radlong")
            self.assertEqual(p_pol.get("projection"), "polar")
        finally:
            gs.run_command("g.remove", type="raster",
                           name=f"{out_rl},{out_pol}", flags="f", quiet=True)


if __name__ == "__main__":
    test()
