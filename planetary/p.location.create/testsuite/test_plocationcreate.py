"""Tests for p.location.create — standalone (dry-run) mode only."""

import subprocess
import sys
import unittest

MOD = "p.location.create.py"
import os
MOD_PATH = os.path.join(os.path.dirname(__file__), "..", MOD)


def _run(*args):
    r = subprocess.run([sys.executable, MOD_PATH] + list(args),
                       capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


class TestLocationCreateDryRun(unittest.TestCase):

    def test_mars_latlong(self):
        rc, out, _ = _run("-p", "--body", "mars", "--projection", "latlong")
        self.assertEqual(rc, 0)
        self.assertIn("+proj=longlat", out)
        self.assertIn("3396190", out)
        self.assertIn("3376200", out)

    def test_moon_eqc(self):
        rc, out, _ = _run("-p", "--body", "moon", "--projection", "eqc")
        self.assertEqual(rc, 0)
        self.assertIn("+proj=eqc", out)
        self.assertIn("1737400", out)

    def test_north_stereo(self):
        rc, out, _ = _run("-p", "--body", "mars", "--projection", "north_stereo")
        self.assertEqual(rc, 0)
        self.assertIn("+proj=stere", out)
        self.assertIn("lat_0=90", out)

    def test_south_stereo(self):
        rc, out, _ = _run("-p", "--body", "mars", "--projection", "south_stereo")
        self.assertEqual(rc, 0)
        self.assertIn("lat_0=-90", out)

    def test_sinu(self):
        rc, out, _ = _run("-p", "--body", "venus", "--projection", "sinu")
        self.assertEqual(rc, 0)
        self.assertIn("+proj=sinu", out)
        self.assertIn("6051800", out)

    def test_lcc(self):
        rc, out, _ = _run("-p", "--body", "mars", "--projection", "lcc",
                          "--lat_1", "30", "--lat_2", "60")
        self.assertEqual(rc, 0)
        self.assertIn("+proj=lcc", out)
        self.assertIn("lat_1=30", out)
        self.assertIn("lat_2=60", out)

    def test_mercury(self):
        rc, out, _ = _run("-p", "--body", "mercury", "--projection", "latlong")
        self.assertEqual(rc, 0)
        self.assertIn("2439700", out)

    def test_titan(self):
        rc, out, _ = _run("-p", "--body", "titan", "--projection", "eqc")
        self.assertEqual(rc, 0)
        self.assertIn("2574730", out)

    def test_custom_body(self):
        rc, out, _ = _run("-p", "--body", "custom",
                          "--semi_major", "1000000", "--semi_minor", "990000",
                          "--projection", "latlong")
        self.assertEqual(rc, 0)
        self.assertIn("1000000", out)
        self.assertIn("990000", out)

    def test_custom_requires_semi_major(self):
        rc, out, err = _run("-p", "--body", "custom")
        self.assertNotEqual(rc, 0)
        self.assertIn("semi_major", err)

    def test_default_location_name_is_body(self):
        rc, out, _ = _run("-p", "--body", "moon", "--projection", "latlong")
        self.assertEqual(rc, 0)
        self.assertIn("location:   moon", out)

    def test_lon_0_propagates(self):
        rc, out, _ = _run("-p", "--body", "mars", "--projection", "eqc",
                          "--lon_0", "180")
        self.assertEqual(rc, 0)
        self.assertIn("lon_0=180", out)


if __name__ == "__main__":
    unittest.main()
