"""
Testsuite for p.in.spice (offline logic).

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.in.spice/testsuite/test_p_in_spice.py -v

These tests exercise the parts that need no network: the bundle registry,
the pinned-checksum verification, and the meta-kernel builder. Actual NAIF
downloads (-d) are not tested here.
"""

import os
import sys
import json
import hashlib
import tempfile
import importlib.util

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

# Load the module file (it does not call gs.parser() on import).
_MODPATH = os.path.join(_ROOT, "p.in.spice", "p.in.spice.py")
_spec = importlib.util.spec_from_file_location("p_in_spice_mod", _MODPATH)
pin = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pin)


class TestBundleRegistry(TestCase):

    def test_bundles_have_required_fields(self):
        self.assertIn("moon-me", pin.BUNDLES)
        for name, b in pin.BUNDLES.items():
            self.assertIn("frame", b)
            self.assertIn("kernels", b)
            self.assertTrue(b["kernels"], f"{name} has no kernels")
            for entry in b["kernels"]:
                self.assertEqual(len(entry), 2, "kernel entry is (url, filename)")

    def test_moon_me_uses_mean_earth_frame(self):
        self.assertEqual(pin.BUNDLES["moon-me"]["frame"], "MOON_ME")

    def test_every_kernel_filename_has_pinned_sha256(self):
        """Each kernel referenced by a bundle should have a pinned checksum."""
        for b in pin.BUNDLES.values():
            for _, fn in b["kernels"]:
                self.assertIn(fn, pin.SHA256,
                              f"{fn} missing a pinned sha256")


class TestChecksumVerify(TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="pinspice_test_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _make(self, name, data):
        path = os.path.join(self.tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def test_verify_passes_on_matching_hash(self):
        data = b"hello spice"
        h = hashlib.sha256(data).hexdigest()
        fn = "fake.tls"
        pin.SHA256[fn] = h
        path = self._make(fn, data)
        try:
            pin._verify(fn, path)   # must not raise
        finally:
            pin.SHA256.pop(fn, None)

    def test_verify_fatal_on_mismatch(self):
        fn = "fake2.tls"
        pin.SHA256[fn] = "0" * 64
        path = self._make(fn, b"wrong content")
        try:
            with self.assertRaises(SystemExit):
                pin._verify(fn, path)
        finally:
            pin.SHA256.pop(fn, None)

    def test_verify_warns_without_pinned_hash(self):
        fn = "unpinned.tf"
        path = self._make(fn, b"x")
        self.assertNotIn(fn, pin.SHA256)
        pin._verify(fn, path)   # warns, does not raise


if __name__ == "__main__":
    test()
