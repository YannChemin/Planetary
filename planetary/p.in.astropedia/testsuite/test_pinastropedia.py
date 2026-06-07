"""Test of p.in.astropedia

Validates the GRASS p.in.astropedia Python module: interface description,
binary presence on PATH, and (when network is available) the -l listing
mode against the USGS Astropedia STAC catalog.  Network-dependent tests
are skipped gracefully when the host has no internet access.

@author Yann Chemin
@license Unlicense (https://unlicense.org)
"""

import shutil
import subprocess
import unittest
import urllib.request
import urllib.error

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule


STAC_BASE = "https://stac.astrogeology.usgs.gov/api"


def _network_available():
    """Return True if stac.astrogeology.usgs.gov is reachable."""
    try:
        urllib.request.urlopen(STAC_BASE, timeout=5)
        return True
    except Exception:
        return False


NETWORK = _network_available()


class TestPinAstropedia(TestCase):
    """Verify p.in.astropedia interface and listing behaviour."""

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.astropedia"),
                              "p.in.astropedia not found on PATH")

    def test_interface_description(self):
        """--interface-description must exit 0 (module parses correctly)."""
        rc = subprocess.run(
            ["p.in.astropedia", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode
        self.assertEqual(rc, 0,
                          "--interface-description returned non-zero exit code")

    def test_missing_source_arg_fails(self):
        """Running without doi/lid/search must exit non-zero."""
        module = SimpleModule("p.in.astropedia", output="dummy")
        self.assertModuleFail(module)

    def test_conflicting_source_args_fail(self):
        """Supplying both doi= and search= must fail with a clear error."""
        module = SimpleModule("p.in.astropedia",
                               doi="10.17189/1519101",
                               search="MOLA",
                               flags="l")
        self.assertModuleFail(module)

    @unittest.skipUnless(NETWORK, "No network — skipping live STAC test")
    def test_list_mode_search(self):
        """-l search= must list at least one STAC result without error."""
        module = SimpleModule("p.in.astropedia",
                               flags="l",
                               search="MOLA 64ppd",
                               limit=3)
        self.assertModule(module)
        combined = (module.outputs.stdout or "") + (module.outputs.stderr or "")
        self.assertRegex(combined, r"[A-Za-z0-9]",
                          "Expected some output from -l search=MOLA")

    @unittest.skipUnless(NETWORK, "No network — skipping live STAC test")
    def test_list_mode_returns_items(self):
        """-l with a generic planetary keyword should list items."""
        module = SimpleModule("p.in.astropedia",
                               flags="l",
                               search="Mars",
                               limit=5)
        self.assertModule(module)

    @unittest.skipUnless(NETWORK, "No network — skipping live PDS LID test")
    def test_list_mode_pds_lid(self):
        """-l with a well-known PDS4 LID must not crash (product may or may
        not be found depending on PDS API availability)."""
        module = SimpleModule(
            "p.in.astropedia",
            flags="l",
            lid="urn:nasa:pds:mgs-mola-dem-mars:data:megt90n000cb",
            limit=1)
        self.assertModule(module)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
