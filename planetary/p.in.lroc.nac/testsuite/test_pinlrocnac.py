"""Test of p.in.lroc.nac.

Validates the GRASS p.in.lroc.nac module: interface description, binary
presence on PATH, bbox parser/intersection logic, and (when network is
available) a smoke listing against the ASU LROC NAC DTM archive.
Network-dependent tests are skipped gracefully when offline.

@author Yann Chemin
@license Unlicense (https://unlicense.org)
"""

import importlib.util
import os
import shutil
import subprocess
import unittest
import urllib.request

from grass.gunittest.case import TestCase

ARCHIVE_HOST = "https://pds.lroc.im-ldi.com"


def _network_available():
    try:
        urllib.request.urlopen(ARCHIVE_HOST, timeout=5)
        return True
    except Exception:
        return False


def _load_module():
    """Import p.in.lroc.nac as a python module from the installed script.

    GRASS installs the script with the literal dotted name on PATH; we
    locate it via shutil.which and load it via importlib so we can call
    its helpers (_parse_bbox, _lon_intersect, filter_index) directly.
    """
    path = shutil.which("p.in.lroc.nac")
    if path is None:
        return None
    spec = importlib.util.spec_from_file_location("p_in_lroc_nac", path)
    mod = importlib.util.module_from_spec(spec)
    # The script body runs gs.parser() at import time inside __main__; we
    # gate it with __name__ check, so a plain import is side-effect-free.
    spec.loader.exec_module(mod)
    return mod


NETWORK = _network_available()
MOD = _load_module()


class TestInterface(TestCase):
    """Interface-level checks that do not need the network."""

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.lroc.nac"),
                             "p.in.lroc.nac not found on PATH")

    def test_interface_description(self):
        rc = subprocess.run(
            ["p.in.lroc.nac", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode
        self.assertEqual(rc, 0, "interface-description must parse cleanly")


@unittest.skipIf(MOD is None, "module helpers not importable")
class TestHelpers(TestCase):
    """Pure-Python helpers — bbox parsing and intersection."""

    def test_parse_bbox_ok(self):
        self.assertEqual(MOD._parse_bbox("0,-90,360,-80"),
                         (0.0, -90.0, 360.0, -80.0))

    def test_parse_bbox_bad_count(self):
        with self.assertRaises(ValueError):
            MOD._parse_bbox("0,-90,360")

    def test_parse_bbox_bad_lat(self):
        with self.assertRaises(ValueError):
            MOD._parse_bbox("0,90,360,-80")

    def test_lon_intersect_simple(self):
        self.assertTrue(MOD._lon_intersect(20, 30, 25, 40))
        self.assertFalse(MOD._lon_intersect(20, 30, 40, 50))

    def test_lon_intersect_dateline(self):
        # Query wraps the dateline (350 → 10), product at 5 → 15 overlaps.
        self.assertTrue(MOD._lon_intersect(350, 10, 5, 15))
        # Both wrap and overlap on the eastern side.
        self.assertTrue(MOD._lon_intersect(350, 5, 355, 8))

    def test_filter_index_by_name(self):
        index = {
            "NOBILE01":   {"bbox": [20, -86, 30, -84]},
            "MALAPERT01": {"bbox": [350, -87, 5, -85]},
            "APOLLO11":   {"bbox": [23, 0, 24, 1]},
        }
        matches = MOD.filter_index(index, name="nobile")
        self.assertEqual([m[0] for m in matches], ["NOBILE01"])

    def test_filter_index_by_bbox_polar(self):
        index = {
            "NOBILE01":   {"bbox": [20, -86, 30, -84]},
            "MALAPERT01": {"bbox": [350, -87, 5, -85]},
            "APOLLO11":   {"bbox": [23, 0, 24, 1]},
        }
        # Full-longitude south-polar query
        matches = MOD.filter_index(index, bbox=(0, -90, 360, -80))
        self.assertEqual(sorted(m[0] for m in matches),
                         ["MALAPERT01", "NOBILE01"])


@unittest.skipUnless(NETWORK, "pds.lroc.im-ldi.com not reachable")
class TestRemoteListing(TestCase):
    """Smoke test against the live archive (network required)."""

    def test_list_products_returns_many(self):
        if MOD is None:
            self.skipTest("module helpers not importable")
        names = MOD.list_products()
        self.assertGreater(len(names), 100,
                           "expected hundreds of NAC DTM products")
        # Sanity: known products should appear.
        self.assertTrue(any(n.startswith("APOLLO") for n in names))

    def test_fetch_bbox_known_product(self):
        if MOD is None:
            self.skipTest("module helpers not importable")
        bb = MOD.fetch_bbox("NOBILE01")
        self.assertIsNotNone(bb, "NOBILE01 bbox parse failed")
        w, s, e, n = bb
        # Nobile is south-polar around -85°.
        self.assertLess(n, -83)
        self.assertGreater(s, -90)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
