"""
Testsuite for p.in.rings.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.rings/testsuite/test_p_in_rings.py -v

Interface tests run unconditionally.  The full ring-projection test (which
requires SPICE kernels) is skipped unless the kernel paths referenced in the
Cassini SOI B-ring example are present under $HOME/RSDATA.

When SPICE kernels ARE available the test:
  1. Creates a synthetic 4×4 raster as a stand-in for the raw camera image.
  2. Sets the computational region to a narrow ring-longitude slice.
  3. Runs p.in.rings and checks that the output raster and planetary.json
     sidecar are created with the expected metadata.
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

# Allow p_meta to be imported for the metadata helper.
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from p_meta import METADATA_FILENAME, SCHEMA_VERSION


# ---------------------------------------------------------------------------
# SPICE kernel availability check
# ---------------------------------------------------------------------------

_RSDATA = Path(os.path.expanduser("~")) / "RSDATA"

# Cassini SOI kernels used in the manual example.
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


# ---------------------------------------------------------------------------
# Full import + metadata test (SPICE-gated)
# ---------------------------------------------------------------------------

class TestPInRingsSpice(TestCase):
    """Ring projection + planetary.json test; skipped without SPICE kernels."""

    raw_input = "test_rings_raw"
    out = "test_rings_out"
    _INSTRUMENT = -82360        # Cassini ISS NAC NAIF ID
    _TIME = "2004-07-01T03:11:40"
    _SPACECRAFT = "CASSINI"
    _BODY = "SATURN"
    _FRAME = "IAU_SATURN"

    @classmethod
    def setUpClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        cls.use_temp_region()
        # Small synthetic "camera image" (4×4, values 0..15).
        gs.run_command("g.region", rows=4, cols=4,
                       n=4, s=0, e=4, w=0, res=1)
        gs.mapcalc(
            f"{cls.raw_input} = (row() - 1) * 4 + (col() - 1)",
            overwrite=True,
        )
        # Ring-plane output region: a narrow longitude slice of the B-ring.
        gs.run_command(
            "g.region",
            n=120000, s=90000, e=10, w=0,
            rows=8, cols=8,
        )

    @classmethod
    def tearDownClass(cls):
        if not HAS_SPICE_KERNELS:
            return
        gs.run_command(
            "g.remove", type="raster",
            name=f"{cls.raw_input},{cls.out}",
            flags="f", quiet=True,
        )
        cls.del_temp_region()

    def _kernel_list(self) -> str:
        return ",".join(str(k) for k in _CASSINI_KERNELS)

    @staticmethod
    def _skip_if_no_spice():
        import unittest
        if not HAS_SPICE_KERNELS:
            raise unittest.SkipTest(
                "SPICE kernels not found under $HOME/RSDATA — "
                "skipping ring-projection test"
            )

    def test_import_creates_raster(self):
        self._skip_if_no_spice()
        self.assertModule(
            "p.in.rings",
            input=self.raw_input,
            output=self.out,
            time=self._TIME,
            instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT,
            body=self._BODY,
            frame=self._FRAME,
            kernels=self._kernel_list(),
            overwrite=True,
        )
        self.assertRasterExists(self.out)

    def test_planetary_json_created(self):
        self._skip_if_no_spice()
        self.assertModule(
            "p.in.rings",
            input=self.raw_input,
            output=self.out,
            time=self._TIME,
            instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT,
            body=self._BODY,
            frame=self._FRAME,
            kernels=self._kernel_list(),
            overwrite=True,
        )
        path = _cell_misc_path(self.out)
        self.assertTrue(path.exists(),
                        f"planetary.json not found at {path}")

    def test_planetary_json_content(self):
        self._skip_if_no_spice()
        self.assertModule(
            "p.in.rings",
            input=self.raw_input,
            output=self.out,
            time=self._TIME,
            instrument=self._INSTRUMENT,
            spacecraft=self._SPACECRAFT,
            body=self._BODY,
            frame=self._FRAME,
            kernels=self._kernel_list(),
            overwrite=True,
        )
        with open(_cell_misc_path(self.out)) as fh:
            data = json.load(fh)

        self.assertEqual(data["schema_version"], SCHEMA_VERSION)
        self.assertEqual(data["data_type"], "rings")
        self.assertIsNotNone(data.get("sensor"))

        planetary = data.get("extended_metadata", {}).get("planetary", {})
        self.assertEqual(planetary.get("body"), self._BODY)
        self.assertEqual(planetary.get("mission"), self._SPACECRAFT)

        # acquisition_datetime must match the supplied time.
        self.assertIn(self._TIME, data.get("acquisition_datetime", ""))


if __name__ == "__main__":
    test()
