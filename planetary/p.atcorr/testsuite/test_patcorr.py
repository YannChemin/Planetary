"""Test of p.atcorr

Purpose: per-pixel, body-aware atmospheric correction dispatcher
         (none/thin/thick regimes).

@author Yann Chemin
"""

import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


def _load_module_under_test():
    """Load p.atcorr.py as a plain Python module (white-box access).

    Filename has a dot in it so it can't be `import`ed normally; loading
    by path also avoids ever executing main() since __name__ != "__main__".
    """
    script_path = os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "p.atcorr.py"))
    spec = importlib.util.spec_from_file_location(
        "patcorr_module_under_test", script_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _create_synthetic_band(mapname, value_or_array, region, overwrite=True):
    """Create a GRASS FCELL raster from a scalar or 2-D numpy array."""
    import numpy as np

    nr, nc = int(region["rows"]), int(region["cols"])
    if np.isscalar(value_or_array):
        arr = np.full((nr, nc), value_or_array, dtype=np.float32)
    else:
        arr = np.asarray(value_or_array, dtype=np.float32)

    tmp = tempfile.mktemp(suffix=".bin")
    arr.tofile(tmp)
    gs.run_command(
        "r.in.bin", input=tmp, output=mapname,
        bytes=4, flags="f",
        north=region["n"], south=region["s"],
        east=region["e"], west=region["w"],
        rows=nr, cols=nc,
        overwrite=overwrite, quiet=True,
    )
    os.unlink(tmp)


def _write_wavelength_csv(path, wavelengths_um):
    with open(path, "w") as f:
        f.write("# wavelength_um,fwhm_um\n")
        for wl in wavelengths_um:
            f.write(f"{wl:.6f},0.005\n")


# Minimal test database: one Mars gas with a retrieval block (left/center/
# right at 1.0/1.3/1.6 µm, easy to hit with a tiny synthetic group), plus
# none/thin/thick regimes for mercury/mars/venus respectively.
_TEST_DB = {
    "_schema": "matter_bands_v1",
    "bodies": {
        "mars": {
            "minerals": [], "ices": [], "organics": [], "liquids": [],
            "gases": [
                {
                    "name": "patcorr_test_co2",
                    "absorption_bands": [
                        {"center": 1.3, "left": 1.0, "right": 1.6},
                    ],
                    "retrieval": {
                        "target": "tau_dust_proxy_550nm",
                        "feature_index": 0,
                        "k_ref": 1.0,
                        "valid_band_depth_range": [0.02, 0.6],
                        "notes": "synthetic test coefficient",
                    },
                },
            ],
        },
        "venus": {
            "minerals": [], "ices": [], "gases": [], "organics": [], "liquids": [],
        },
        "mercury": {
            "minerals": [], "ices": [], "gases": [], "organics": [], "liquids": [],
        },
    },
    "body_meta": {
        "mercury": {"atmosphere": {"regime": "none"}},
        "mars": {
            "atmosphere": {
                "regime": "thin", "tau_clear": 0.3, "tau_dusty": 1.5,
                "wha": 0.9, "retrieval_gas": "patcorr_test_co2",
            }
        },
        "venus": {
            "atmosphere": {
                "regime": "thick",
                "atmosphere_windows": [
                    {"center_um": 1.02, "reference_um": 0.95,
                     "method": "ratio_to_opaque_reference"},
                ],
            }
        },
    },
}


class TestPatcorrUnit(TestCase):
    """White-box tests of the retrieval/correction math, no Hapke calls."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        cls.runModule("g.region", n=4, s=0, e=4, w=0, rows=4, cols=4)
        cls.region = gs.region()
        cls.mod = _load_module_under_test()
        cls.db_path = tempfile.mktemp(suffix=".json")
        with open(cls.db_path, "w") as f:
            json.dump(_TEST_DB, f)

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", flags="f", type="raster",
                       pattern="patcorr_test_*", quiet=True)
        try:
            os.unlink(cls.db_path)
        except OSError:
            pass

    def _load_db(self):
        with open(self.db_path) as f:
            return json.load(f)

    def test_atmosphere_meta_lookup(self):
        db = self._load_db()
        meta = self.mod._atmosphere_meta(db, "mars")
        self.assertEqual(meta["regime"], "thin")

    def test_atmosphere_meta_unknown_body_fatal(self):
        db = self._load_db()
        db["body_meta"].pop("mars")
        with self.assertRaises(SystemExit):
            self.mod._atmosphere_meta(db, "mars")

    def test_retrieve_tau_proxy_in_range(self):
        """A band depth inside the valid range maps to band_depth * k_ref,
        ceiling-clipped to tau_dusty but NOT floored to tau_clear -- a
        genuinely retrieved value smaller than tau_clear is a real signal
        (a clearer-than-default pixel), not an invalid retrieval, and must
        survive. (Regression: an earlier version floored every retrieval
        to tau_clear, which silently zeroed the per-pixel signal whenever
        k_ref * band_depth undershot tau_clear -- caught by running this
        retrieval on real CRISM data, where it did exactly that.)"""
        import numpy as np

        db = self._load_db()
        atm = db["body_meta"]["mars"]["atmosphere"]

        # left/center/right reflectance chosen so continuum-removed depth
        # is a known constant 0.10 everywhere: r_cont(1.3) = (1.0+? )...
        # use equal shoulders (left=right=1.0) so r_cont = 1.0 and
        # bd = 1 - r_center => r_center = 0.90 gives bd = 0.10.
        _create_synthetic_band("patcorr_test_left", 1.0, self.region)
        _create_synthetic_band("patcorr_test_center", 0.90, self.region)
        _create_synthetic_band("patcorr_test_right", 1.0, self.region)
        gs.run_command(
            "i.group", group="patcorr_test_group",
            input="patcorr_test_left,patcorr_test_center,patcorr_test_right",
            overwrite=True, quiet=True)

        wl_dict = {"patcorr_test_left": 1.0, "patcorr_test_center": 1.3,
                   "patcorr_test_right": 1.6}

        tau = self.mod._retrieve_tau_proxy(db, "mars", atm, wl_dict, None)
        # bd = 0.10 is within [0.02, 0.6]; k_ref = 1.0 => tau_proxy = 0.10,
        # below tau_clear (0.3) but that is a real retrieved value, not a
        # fallback condition, so it must pass through unfloored.
        self.assertTrue(np.allclose(tau, 0.10))

    def test_run_thin_handles_retrieved_tau_below_tau_clear(self):
        """Regression test for the real-CRISM-data bug: a retrieved tau
        below tau_clear must still produce a non-NULL corrected pixel
        (clamped into the tau-bin table's range for bin selection), not
        silently NULL the output."""
        import shutil
        if not shutil.which("p.atcorr.hapke"):
            self.skipTest("p.atcorr.hapke not on PATH")
        import numpy as np

        db = self._load_db()
        db["body_meta"]["mars"]["atmosphere"]["tau_clear"] = 0.3
        db["body_meta"]["mars"]["atmosphere"]["tau_dusty"] = 1.5

        # Retrieval triplet: bd = 0.10 (in-range) * k_ref=1.0 -> tau_proxy
        # = 0.10, well below tau_clear = 0.3.
        _create_synthetic_band("patcorr_test_thin_left", 1.0, self.region)
        _create_synthetic_band("patcorr_test_thin_center", 0.90, self.region)
        _create_synthetic_band("patcorr_test_thin_right", 1.0, self.region)
        # The band actually being corrected (arbitrary reflectance).
        _create_synthetic_band("patcorr_test_thin_real", 0.20, self.region)
        _create_synthetic_band("patcorr_test_inc", 30.0, self.region)
        _create_synthetic_band("patcorr_test_emi", 5.0, self.region)
        _create_synthetic_band("patcorr_test_pha", 25.0, self.region)

        wl_dict = {
            "patcorr_test_thin_left": 1.0, "patcorr_test_thin_center": 1.3,
            "patcorr_test_thin_right": 1.6, "patcorr_test_thin_real": 1.3,
        }
        opts = {
            "incidence": "patcorr_test_inc", "emission": "patcorr_test_emi",
            "phase": "patcorr_test_pha", "model": "isotropic2",
            "tau_bins": 2, "smooth": None,
        }
        self.mod._run_thin(db, "mars",
                           ["patcorr_test_thin_left", "patcorr_test_thin_center",
                            "patcorr_test_thin_right", "patcorr_test_thin_real"],
                           wl_dict, "patcorr_test_thin_out", opts, False)

        out = self.mod.pmb._read_band("patcorr_test_thin_out.patcorr_test_thin_real")
        self.assertFalse(np.any(np.isnan(out)),
                         "retrieved tau below tau_clear must not NULL the output")

    def test_retrieve_tau_proxy_out_of_range_falls_back_to_tau_clear(self):
        import numpy as np

        db = self._load_db()
        atm = db["body_meta"]["mars"]["atmosphere"]

        # r_center = r_cont (no shoulders difference) => bd = 0, below the
        # valid range floor of 0.02 => falls back to tau_clear.
        _create_synthetic_band("patcorr_test_left2", 1.0, self.region)
        _create_synthetic_band("patcorr_test_center2", 1.0, self.region)
        _create_synthetic_band("patcorr_test_right2", 1.0, self.region)

        wl_dict = {"patcorr_test_left2": 1.0, "patcorr_test_center2": 1.3,
                   "patcorr_test_right2": 1.6}
        tau = self.mod._retrieve_tau_proxy(db, "mars", atm, wl_dict, None)
        self.assertTrue(np.allclose(tau, atm["tau_clear"]))

    def test_run_none_passthrough(self):
        import numpy as np

        _create_synthetic_band("patcorr_test_band1", 0.25, self.region)
        out = self.mod._run_none(["patcorr_test_band1"], "patcorr_test_out_none")
        arr_in = self.mod.pmb._read_band("patcorr_test_band1")
        arr_out = self.mod.pmb._read_band("patcorr_test_out_none.patcorr_test_band1")
        self.assertTrue(np.allclose(arr_in, arr_out))

    def test_thick_window_ratio_normalisation(self):
        import numpy as np

        db = self._load_db()
        # window band varies pixel-to-pixel (simulated local cloud opacity);
        # reference band is uniformly opaque (constant).
        window_arr = np.array([[0.10, 0.20], [0.30, 0.40]], dtype=np.float32)
        _create_synthetic_band("patcorr_test_window", window_arr,
                               {"n": 2, "s": 0, "e": 2, "w": 0, "rows": 2, "cols": 2})
        self.runModule("g.region", n=2, s=0, e=2, w=0, rows=2, cols=2)
        region2 = gs.region()
        _create_synthetic_band("patcorr_test_window", window_arr, region2)
        _create_synthetic_band("patcorr_test_ref", 0.50, region2)
        gs.run_command(
            "i.group", group="patcorr_test_venus_group",
            input="patcorr_test_window,patcorr_test_ref",
            overwrite=True, quiet=True)

        wl_dict = {"patcorr_test_window": 1.02, "patcorr_test_ref": 0.95}
        self.mod._run_thick(db, "venus", ["patcorr_test_window", "patcorr_test_ref"],
                            wl_dict, "patcorr_test_out_thick", 0.03, False)

        corrected = self.mod.pmb._read_band(
            "patcorr_test_out_thick.patcorr_test_window")
        ratio = window_arr / 0.50
        median_ratio = float(np.median(ratio))
        expected = window_arr * (median_ratio / ratio)
        self.assertTrue(np.allclose(corrected, expected, atol=1e-5))

        # Reference band itself has no registered window -> NULL output.
        ref_out = self.mod.pmb._read_band("patcorr_test_out_thick.patcorr_test_ref")
        self.assertTrue(np.all(np.isnan(ref_out)))

        self.runModule("g.region", n=4, s=0, e=4, w=0, rows=4, cols=4)


class TestPatcorrCli(TestCase):
    """Black-box CLI smoke tests."""

    def test_help(self):
        script = os.path.normpath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "p.atcorr.py"))
        result = subprocess.run(
            ["python3", script, "--help"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0,
                         msg=f"--help exited {result.returncode}:\n{result.stderr}")
        self.assertIn("p.atcorr", result.stdout + result.stderr)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
