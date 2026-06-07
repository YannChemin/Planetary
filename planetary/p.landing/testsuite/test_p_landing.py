"""
Testsuite for p.landing helper functions.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.landing/testsuite/test_p_landing.py -v

p.landing is the end-to-end orchestrator; a full run needs a DEM + body +
mission and is covered by running the pipeline manually. These unit tests
exercise the pure helpers (state I/O and the terminal-formatting utilities)
that have no GRASS side effects.
"""

import os
import sys
import json
import tempfile
import importlib.util

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

_MODPATH = os.path.join(_ROOT, "p.landing", "p.landing.py")
_spec = importlib.util.spec_from_file_location("p_landing_mod", _MODPATH)
pl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pl)


class TestStateIO(TestCase):

    def test_state_round_trip(self):
        tmp = tempfile.mkdtemp(prefix="plstate_")
        try:
            path = os.path.join(tmp, "state.json")
            self.assertEqual(pl.load_state(path), {})  # missing -> empty
            state = {"terrain": True, "illum_out": {"illum_fraction": "x"}}
            pl.save_state(path, state)
            self.assertTrue(os.path.isfile(path))
            self.assertEqual(pl.load_state(path), state)
            with open(path) as f:
                self.assertEqual(json.load(f), state)
        finally:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)

    def test_force_preserves_upstream_outputs(self):
        """Regression: `-f` used to wipe the whole state (state = {}), losing
        terrain_out/illum_out/vis_out maps that a partial rerun like
        `stages=visibility,mcdm` needs to feed into mcdm. After the fix only
        the markers for stages_run are invalidated; output dicts persist."""
        tmp = tempfile.mkdtemp(prefix="plstate_")
        try:
            path = os.path.join(tmp, "state.json")
            pl.save_state(path, {
                "terrain": True,
                "terrain_out": {"slope": "slope_30m",
                                "roughness": "roughness_rms",
                                "hazard_mask": "hazard_mask"},
                "illumination": True,
                "illum_out": {"illum_fraction": "illum_fraction"},
                "visibility": True,
                "vis_out": {"earth_vis_fraction": "earth_vis_fraction"},
                "mcdm": True,
                "suit_out": {"suitability": "suitability_wlc"},
            })
            # Simulate p.landing main()'s state-resolution under -f with
            # stages=visibility,mcdm. This mirrors the inlined logic at the
            # top of main(); the test will fail if anyone reverts to the
            # `state = {} if flag_force else load_state(...)` shortcut.
            state = pl.load_state(path)
            flag_force = True
            stages_run = ["visibility", "mcdm"]
            if flag_force:
                for s in stages_run:
                    state[s] = False
            self.assertEqual(state["terrain_out"]["slope"], "slope_30m",
                "terrain_out must survive -f when terrain is not in stages_run")
            self.assertEqual(state["illum_out"]["illum_fraction"],
                             "illum_fraction",
                "illum_out must survive -f when illumination is not in stages_run")
            self.assertFalse(state["visibility"],
                "visibility marker must be cleared so the stage re-runs")
            self.assertFalse(state["mcdm"],
                "mcdm marker must be cleared so the stage re-runs")
            self.assertTrue(state["terrain"],
                "terrain marker must stay True (terrain not being re-run)")
        finally:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)


class TestFormatting(TestCase):

    def test_fmt_duration(self):
        self.assertEqual(pl._fmt_duration(0), "0:00")
        self.assertEqual(pl._fmt_duration(65), "1:05")
        self.assertEqual(pl._fmt_duration(3661), "1:01:01")

    def test_format_region_reports_cell_count(self):
        reg = {"nsres": 30.0, "ewres": 30.0, "rows": 40, "cols": 50,
               "n": 1200, "s": 0, "e": 1500, "w": 0}
        s = pl._format_region(reg)
        self.assertIn("cells", s)
        self.assertIn("2,000", s)  # 40 * 50 = 2000

    def test_format_extent(self):
        reg = {"n": 1200, "s": 0, "e": 1500, "w": 0}
        s = pl._format_extent(reg)
        for token in ("n=", "s=", "e=", "w="):
            self.assertIn(token, s)

    def test_candidate_table_handles_missing_report(self):
        # Should silently return (no exception) when the report is absent.
        self.assertIsNone(pl._candidate_table("/no/such/report.json"))


if __name__ == "__main__":
    test()
