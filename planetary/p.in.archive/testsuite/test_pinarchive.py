"""Test of p.in.archive

Validates the GRASS p.in.archive Python module: interface description,
binary presence on PATH, and (when network is available) the -l listing
mode against the USGS Astropedia STAC catalog.  Network-dependent tests
are skipped gracefully when the host has no internet access.

@author Yann Chemin
@license Unlicense (https://unlicense.org)
"""

import importlib.util
import os
import shutil
import subprocess
import unittest
import urllib.request
import urllib.error

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule


STAC_BASE = "https://stac.astrogeology.usgs.gov/api"
CRISM_ARCHIVE_BASE = "https://pds-geosciences.wustl.edu"


def _network_available():
    """Return True if stac.astrogeology.usgs.gov is reachable."""
    try:
        urllib.request.urlopen(STAC_BASE, timeout=5)
        return True
    except Exception:
        return False


def _crism_archive_available():
    try:
        urllib.request.urlopen(CRISM_ARCHIVE_BASE, timeout=5)
        return True
    except Exception:
        return False


NETWORK = _network_available()
CRISM_NETWORK = _crism_archive_available()


def _load_module_under_test():
    """Load p.in.archive.py by file path (filename has dots, so it can't
    be imported as a regular module name)."""
    here = os.path.dirname(os.path.abspath(__file__))
    script_path = os.path.join(here, "..", "p.in.archive.py")
    spec = importlib.util.spec_from_file_location("p_in_archive_mut", script_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class TestPinArchive(TestCase):
    """Verify p.in.archive interface and listing behaviour."""

    def test_module_on_path(self):
        self.assertIsNotNone(shutil.which("p.in.archive"),
                              "p.in.archive not found on PATH")

    def test_interface_description(self):
        """--interface-description must exit 0 (module parses correctly)."""
        rc = subprocess.run(
            ["p.in.archive", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode
        self.assertEqual(rc, 0,
                          "--interface-description returned non-zero exit code")

    def test_missing_source_arg_fails(self):
        """Running without doi/lid/search must exit non-zero."""
        module = SimpleModule("p.in.archive", output="dummy")
        self.assertModuleFail(module)

    def test_conflicting_source_args_fail(self):
        """Supplying both doi= and search= must fail with a clear error."""
        module = SimpleModule("p.in.archive",
                               doi="10.17189/1519101",
                               search="MOLA",
                               flags="l")
        self.assertModuleFail(module)

    @unittest.skipUnless(NETWORK, "No network — skipping live STAC test")
    def test_list_mode_search(self):
        """-l search= must list at least one STAC result without error."""
        module = SimpleModule("p.in.archive",
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
        module = SimpleModule("p.in.archive",
                               flags="l",
                               search="Mars",
                               limit=5)
        self.assertModule(module)

    @unittest.skipUnless(NETWORK, "No network — skipping live PDS LID test")
    def test_list_mode_pds_lid(self):
        """-l with a well-known PDS4 LID must not crash (product may or may
        not be found depending on PDS API availability)."""
        module = SimpleModule(
            "p.in.archive",
            flags="l",
            lid="urn:nasa:pds:mgs-mola-dem-mars:data:megt90n000cb",
            limit=1)
        self.assertModule(module)


class TestPinArchiveCrismCatalog(unittest.TestCase):
    """White-box tests for the crism= catalog resolver (no network)."""

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module_under_test()

    def test_catalog_has_mawrth_vallis_entries(self):
        self.assertIn("mawrth_vallis_frt00003bfb_ir", self.mod.CRISM_CATALOG)
        self.assertIn("mawrth_vallis_frt00003bfb_vnir", self.mod.CRISM_CATALOG)

    def test_resolve_crism_catalog_key(self):
        img_url, lbl_url, body = self.mod.resolve_crism(
            "mawrth_vallis_frt00003bfb_vnir")
        self.assertTrue(img_url.endswith(".IMG"))
        self.assertTrue(lbl_url.endswith(".LBL"))
        self.assertEqual(body, "Mars")
        self.assertIn("pds-geosciences.wustl.edu", img_url)

    def test_resolve_crism_direct_url(self):
        url = (f"{CRISM_ARCHIVE_BASE}/mro/mro-m-crism-3-rdr-targeted-v1/"
               "mrocr_2101/trdr/2007/2007_005/FRT00003BFB/"
               "FRT00003BFB_01_IF156L_TRR3.IMG")
        img_url, lbl_url, body = self.mod.resolve_crism(url)
        self.assertEqual(img_url, url)
        self.assertTrue(lbl_url.endswith("FRT00003BFB_01_IF156L_TRR3.LBL"))
        self.assertIsNone(body)

    def test_resolve_crism_rejects_non_img_url(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_crism(f"{CRISM_ARCHIVE_BASE}/some/file.tif")

    def test_resolve_crism_unknown_key_fails(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_crism("not_a_real_catalog_key")


class TestPinArchiveCrismCli(TestCase):
    """Black-box CLI tests for crism= (mutual exclusion, listing)."""

    def test_crism_conflicts_with_cog(self):
        module = SimpleModule("p.in.archive",
                               crism="mawrth_vallis_frt00003bfb_vnir",
                               cog="mars_mola_dem_463m",
                               output="dummy")
        self.assertModuleFail(module)

    def test_crism_conflicts_with_doi(self):
        module = SimpleModule("p.in.archive",
                               crism="mawrth_vallis_frt00003bfb_vnir",
                               doi="10.17189/1519101",
                               output="dummy")
        self.assertModuleFail(module)

    def test_crism_requires_output(self):
        module = SimpleModule("p.in.archive",
                               crism="mawrth_vallis_frt00003bfb_vnir")
        self.assertModuleFail(module)

    def test_crism_unknown_key_fails(self):
        module = SimpleModule("p.in.archive",
                               crism="not_a_real_catalog_key",
                               output="dummy")
        self.assertModuleFail(module)

    def test_list_includes_crism_catalog(self):
        """-l (no other source) must list both the COG and CRISM catalogs."""
        module = SimpleModule("p.in.archive", flags="l")
        self.assertModule(module)
        combined = (module.outputs.stdout or "") + (module.outputs.stderr or "")
        self.assertIn("mawrth_vallis_frt00003bfb_ir", combined)

    def test_list_includes_m3_and_vims_catalogs(self):
        """-l (no other source) must also list the M3 and VIMS catalogs."""
        module = SimpleModule("p.in.archive", flags="l")
        self.assertModule(module)
        combined = (module.outputs.stdout or "") + (module.outputs.stderr or "")
        self.assertIn("m3g20081118t222604_v03_rdn", combined)
        self.assertIn("titan_v1799424623", combined)


class TestPinArchiveM3Catalog(unittest.TestCase):
    """White-box tests for the m3= catalog resolver (no network)."""

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module_under_test()

    def test_catalog_has_seed_entry(self):
        self.assertIn("m3g20081118t222604_v03_rdn", self.mod.M3_CATALOG)

    def test_resolve_m3_catalog_key(self):
        img_url, lbl_url, body = self.mod.resolve_m3(
            "m3g20081118t222604_v03_rdn")
        self.assertTrue(img_url.endswith("_RDN.IMG"))
        self.assertTrue(lbl_url.endswith(".LBL"))
        self.assertEqual(body, "Moon")
        self.assertIn("planetarydata.jpl.nasa.gov", img_url)

    def test_resolve_m3_direct_url(self):
        url = ("https://planetarydata.jpl.nasa.gov/img/data/m3/CH1M3_0003/"
               "DATA/20081118_20090214/200811/L1B/"
               "M3G20081118T222604_V03_RDN.IMG")
        img_url, lbl_url, body = self.mod.resolve_m3(url)
        self.assertEqual(img_url, url)
        self.assertTrue(lbl_url.endswith("M3G20081118T222604_V03_L1B.LBL"))
        self.assertIsNone(body)

    def test_resolve_m3_rejects_non_rdn_url(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_m3(
                "https://planetarydata.jpl.nasa.gov/some/file.tif")

    def test_resolve_m3_unknown_key_fails(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_m3("not_a_real_catalog_key")


class TestPinArchiveM3Cli(TestCase):
    """Black-box CLI tests for m3= (mutual exclusion, listing)."""

    def test_m3_conflicts_with_crism(self):
        module = SimpleModule("p.in.archive",
                               m3="m3g20081118t222604_v03_rdn",
                               crism="mawrth_vallis_frt00003bfb_vnir",
                               output="dummy")
        self.assertModuleFail(module)

    def test_m3_requires_output(self):
        module = SimpleModule("p.in.archive",
                               m3="m3g20081118t222604_v03_rdn")
        self.assertModuleFail(module)

    def test_m3_unknown_key_fails(self):
        module = SimpleModule("p.in.archive",
                               m3="not_a_real_catalog_key",
                               output="dummy")
        self.assertModuleFail(module)


class TestPinArchiveVimsCatalog(unittest.TestCase):
    """White-box tests for the vims= catalog resolver (no network)."""

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module_under_test()

    def test_catalog_has_seed_entry(self):
        self.assertIn("titan_v1799424623", self.mod.VIMS_CATALOG)

    def test_resolve_vims_catalog_key(self):
        opus_id, body = self.mod.resolve_vims("titan_v1799424623")
        self.assertEqual(opus_id, "co-vims-v1799424623")
        self.assertEqual(body, "Titan")

    def test_resolve_vims_direct_opus_id(self):
        opus_id, body = self.mod.resolve_vims("co-vims-v9999999999")
        self.assertEqual(opus_id, "co-vims-v9999999999")
        self.assertIsNone(body)

    def test_resolve_vims_unknown_key_fails(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_vims("not_a_real_catalog_key")


class TestPinArchiveVimsCli(TestCase):
    """Black-box CLI tests for vims= (mutual exclusion, listing)."""

    def test_vims_conflicts_with_crism(self):
        module = SimpleModule("p.in.archive",
                               vims="titan_v1799424623",
                               crism="mawrth_vallis_frt00003bfb_vnir",
                               output="dummy")
        self.assertModuleFail(module)

    def test_vims_requires_output(self):
        module = SimpleModule("p.in.archive",
                               vims="titan_v1799424623")
        self.assertModuleFail(module)

    def test_vims_unknown_key_fails(self):
        module = SimpleModule("p.in.archive",
                               vims="not_a_real_catalog_key",
                               output="dummy")
        self.assertModuleFail(module)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
