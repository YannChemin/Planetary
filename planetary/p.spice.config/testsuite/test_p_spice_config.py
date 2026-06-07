"""
Testsuite for p.spice.config (offline logic).

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.spice.config/testsuite/test_p_spice_config.py -v

Covers the body/frame radius table and the meta-kernel frame parser. The
mapset VAR round-trip (g.gisenv store=mapset) and the live SPICE test call
are exercised by running the installed module in a real mapset.
"""

import os
import sys
import tempfile
import importlib.util

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

_MODPATH = os.path.join(_ROOT, "p.spice.config", "p.spice.config.py")
_spec = importlib.util.spec_from_file_location("p_spice_config_mod", _MODPATH)
cfg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cfg)


class TestRadiusBodyTable(TestCase):

    def test_moon_maps_to_mean_earth_frame(self):
        match = [(b, fr) for (r, b, fr) in cfg.RADIUS_BODY
                 if abs(r - 1737400.0) < 1.0]
        self.assertEqual(match, [("MOON", "MOON_ME")])

    def test_mars_present(self):
        bodies = {b: fr for (_, b, fr) in cfg.RADIUS_BODY}
        self.assertEqual(bodies.get("MARS"), "IAU_MARS")

    def test_keys_defined(self):
        for k in ("P_SPICE_META", "P_SPICE_TARGET",
                  "P_SPICE_FRAME", "P_SPICE_OBSERVER"):
            self.assertIn(k, cfg.ALL_KEYS)


class TestMetaFrameParser(TestCase):

    def test_frame_extracted_from_meta_comment(self):
        meta = tempfile.NamedTemporaryFile("w", suffix=".tm", delete=False)
        meta.write("KPL/MK\n\\begintext\n"
                   "  Body-fixed frame to pass to the planetary modules: "
                   "MOON_ME.\n\\begindata\n")
        meta.close()
        try:
            self.assertEqual(cfg._frame_from_meta(meta.name), "MOON_ME")
        finally:
            os.unlink(meta.name)

    def test_frame_none_when_absent(self):
        meta = tempfile.NamedTemporaryFile("w", suffix=".tm", delete=False)
        meta.write("KPL/MK\n\\begindata\n  PATH_VALUES = ( '/x' )\n")
        meta.close()
        try:
            self.assertIsNone(cfg._frame_from_meta(meta.name))
        finally:
            os.unlink(meta.name)


if __name__ == "__main__":
    test()
