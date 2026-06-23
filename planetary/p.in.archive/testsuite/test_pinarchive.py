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
M3_ARCHIVE_BASE = "https://planetarydata.jpl.nasa.gov"
VIMS_ARCHIVE_BASE = "https://opus.pds-rings.seti.org"
OMEGA_ARCHIVE_BASE = "https://archives.esac.esa.int"

# Real end-to-end downloads (CRISM/M3/VIMS/OMEGA import tests below) are
# heavy -- CRISM/OMEGA/VIMS are a few hundred KB to ~20 MB, M3's RDN.IMG
# alone is ~120 MB -- so they only run when explicitly requested, not on
# every default test run. Set this to run them (and to reproduce any of
# them manually, run the exact same p.in.archive command shown in each
# test's docstring).
RUN_E2E = os.environ.get("P_IN_ARCHIVE_RUN_E2E") == "1"


def _url_available(url):
    try:
        urllib.request.urlopen(url, timeout=5)
        return True
    except Exception:
        return False


def _network_available():
    """Return True if stac.astrogeology.usgs.gov is reachable."""
    return _url_available(STAC_BASE)


def _crism_archive_available():
    return _url_available(CRISM_ARCHIVE_BASE)


NETWORK = _network_available()
CRISM_NETWORK = _crism_archive_available()
M3_NETWORK = _url_available(M3_ARCHIVE_BASE)
VIMS_NETWORK = _url_available(VIMS_ARCHIVE_BASE)
OMEGA_NETWORK = _url_available(OMEGA_ARCHIVE_BASE)


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

    def test_list_includes_omega_catalog(self):
        """-l (no other source) must also list the OMEGA catalog."""
        module = SimpleModule("p.in.archive", flags="l")
        self.assertModule(module)
        combined = (module.outputs.stdout or "") + (module.outputs.stderr or "")
        self.assertIn("orb0100_0", combined)


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


class TestPinArchiveOmegaCatalog(unittest.TestCase):
    """White-box tests for the omega= catalog resolver (no network)."""

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module_under_test()

    def test_catalog_has_seed_entry(self):
        self.assertIn("orb0100_0", self.mod.OMEGA_CATALOG)

    def test_resolve_omega_catalog_key(self):
        img_url, body = self.mod.resolve_omega("orb0100_0")
        self.assertTrue(img_url.endswith(".QUB"))
        self.assertEqual(body, "Mars")
        self.assertIn("archives.esac.esa.int", img_url)

    def test_resolve_omega_direct_url(self):
        url = ("https://archives.esac.esa.int/psa/ftp/MARS-EXPRESS/OMEGA/"
               "MEX-M-OMEGA-2-EDR-FLIGHT-V1.0/DATA/ORB01/ORB0100_0.QUB")
        img_url, body = self.mod.resolve_omega(url)
        self.assertEqual(img_url, url)
        self.assertIsNone(body)

    def test_resolve_omega_rejects_non_qub_url(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_omega(f"{OMEGA_ARCHIVE_BASE}/some/file.tif")

    def test_resolve_omega_unknown_key_fails(self):
        with self.assertRaises(SystemExit):
            self.mod.resolve_omega("not_a_real_catalog_key")


class TestPinArchiveOmegaCli(TestCase):
    """Black-box CLI tests for omega= (mutual exclusion, listing)."""

    def test_omega_conflicts_with_crism(self):
        module = SimpleModule("p.in.archive",
                               omega="orb0100_0",
                               crism="mawrth_vallis_frt00003bfb_vnir",
                               output="dummy")
        self.assertModuleFail(module)

    def test_omega_requires_output(self):
        module = SimpleModule("p.in.archive", omega="orb0100_0")
        self.assertModuleFail(module)

    def test_omega_unknown_key_fails(self):
        module = SimpleModule("p.in.archive",
                               omega="not_a_real_catalog_key",
                               output="dummy")
        self.assertModuleFail(module)


class TestPinArchiveE2EImport(TestCase):
    """Real, network-heavy end-to-end download+import+verify tests, one per
    p.in.archive hyperspectral sensor (crism=/m3=/vims=/omega=).

    Opt-in only (set P_IN_ARCHIVE_RUN_E2E=1) -- these are real downloads
    against the live archives (a few hundred KB to ~120 MB), not suitable
    for routine CI. Each test's docstring is the exact manual command to
    reproduce and cross-check its result by hand. Run them all with:

        P_IN_ARCHIVE_RUN_E2E=1 grass --tmp-project XY --exec \\
            python3 testsuite/test_pinarchive.py \\
            TestPinArchiveE2EImport

    MUST run against an unprojected (XY) GRASS project, not a geographic
    one: crism=/m3=/vims=/omega= import raw, non-georeferenced
    pixel/line instrument-frame cubes (that's the whole reason
    p.phocube/p.spiceinit exist -- to attach real geometry afterwards),
    and their pixel/line region pre-sizing call fails with "Illegal
    latitude for North" if pointed at a geographic project instead
    (confirmed live against this repo's own mars_mineralogy/PERMANENT).
    `--tmp-project XY` is also what's used throughout this session's own
    manual verification of crism=/m3=/vims=/omega=.

    Each test cleans up the maps/groups it creates.
    """

    def tearDown(self):
        import grass.script as gs
        for name in getattr(self, "_e2e_groups", []):
            gs.run_command("g.remove", flags="rf", type="group", name=name,
                            quiet=True, errors="ignore")

    @unittest.skipUnless(RUN_E2E and CRISM_NETWORK,
                          "set P_IN_ARCHIVE_RUN_E2E=1 and have network "
                          "access to run this real download+import test")
    def test_e2e_crism(self):
        """Manual cross-check:
            p.in.archive crism=mawrth_vallis_frt00003bfb_vnir \\
                output=e2e_crism_test --overwrite
            r.univar -g e2e_crism_test.1
        Expect a non-degenerate VNIR I/F cube (107 bands)."""
        self._e2e_groups = ["e2e_crism_test"]
        module = SimpleModule(
            "p.in.archive", crism="mawrth_vallis_frt00003bfb_vnir",
            output="e2e_crism_test", overwrite=True)
        self.assertModule(module)
        self.assertRasterExists("e2e_crism_test.1")
        import grass.script as gs
        gs.run_command("g.region", raster="e2e_crism_test.1", quiet=True)
        stats = gs.parse_command("r.univar", map="e2e_crism_test.1", flags="g")
        self.assertGreater(int(stats["n"]), 0)
        self.assertGreater(float(stats["stddev"]), 0)

    @unittest.skipUnless(RUN_E2E and M3_NETWORK,
                          "set P_IN_ARCHIVE_RUN_E2E=1 and have network "
                          "access to run this real download+import test")
    def test_e2e_m3(self):
        """Manual cross-check:
            p.in.archive m3=m3g20081118t222604_v03_rdn -g \\
                output=e2e_m3_test --overwrite
            r.univar -g e2e_m3_test_loc.2   # latitude: ~86-90 (near pole)
            r.univar -g e2e_m3_test_obs.5   # phase angle: ~82-98 deg
        Expect an 85-band radiance cube plus 3-band LOC (lon/lat/radius)
        and 10-band OBS (illumination/viewing angles) geometry groups."""
        self._e2e_groups = ["e2e_m3_test", "e2e_m3_test_loc", "e2e_m3_test_obs"]
        module = SimpleModule(
            "p.in.archive", m3="m3g20081118t222604_v03_rdn", flags="g",
            output="e2e_m3_test", overwrite=True)
        self.assertModule(module)
        self.assertRasterExists("e2e_m3_test.1")
        self.assertRasterExists("e2e_m3_test_loc.2")  # latitude
        self.assertRasterExists("e2e_m3_test_obs.5")  # phase angle
        import grass.script as gs
        gs.run_command("g.region", raster="e2e_m3_test_loc.2", quiet=True)
        lat = gs.parse_command("r.univar", map="e2e_m3_test_loc.2", flags="g")
        self.assertGreater(float(lat["min"]), 80.0)
        self.assertLessEqual(float(lat["max"]), 90.0)
        phase = gs.parse_command("r.univar", map="e2e_m3_test_obs.5", flags="g")
        self.assertGreater(float(phase["min"]), 50.0)
        self.assertLess(float(phase["max"]), 130.0)

    @unittest.skipUnless(RUN_E2E and VIMS_NETWORK,
                          "set P_IN_ARCHIVE_RUN_E2E=1 and have network "
                          "access to run this real download+import test")
    def test_e2e_vims(self):
        """Manual cross-check:
            p.in.archive vims=titan_v1799424623 output=e2e_vims_test --overwrite
            r.univar -g e2e_vims_test.50
        Expect a non-degenerate 352-band raw-DN cube within the
        instrument's declared saturation bounds (-32768..32767)."""
        self._e2e_groups = ["e2e_vims_test"]
        module = SimpleModule(
            "p.in.archive", vims="titan_v1799424623",
            output="e2e_vims_test", overwrite=True)
        self.assertModule(module)
        self.assertRasterExists("e2e_vims_test.50")
        import grass.script as gs
        gs.run_command("g.region", raster="e2e_vims_test.50", quiet=True)
        stats = gs.parse_command("r.univar", map="e2e_vims_test.50", flags="g")
        self.assertGreater(int(stats["n"]), 0)
        self.assertGreater(float(stats["stddev"]), 0)
        self.assertGreaterEqual(float(stats["min"]), -32768)
        self.assertLessEqual(float(stats["max"]), 32767)

    @unittest.skipUnless(RUN_E2E and OMEGA_NETWORK,
                          "set P_IN_ARCHIVE_RUN_E2E=1 and have network "
                          "access to run this real download+import test")
    def test_e2e_omega(self):
        """Manual cross-check:
            p.in.archive omega=orb0100_0 output=e2e_omega_test --overwrite
            r.univar -g e2e_omega_test.1
        Expect a non-degenerate 352-band raw-DN cube within the
        instrument's declared saturation bounds (-32768..32767)."""
        self._e2e_groups = ["e2e_omega_test"]
        module = SimpleModule(
            "p.in.archive", omega="orb0100_0",
            output="e2e_omega_test", overwrite=True)
        self.assertModule(module)
        self.assertRasterExists("e2e_omega_test.1")
        import grass.script as gs
        gs.run_command("g.region", raster="e2e_omega_test.1", quiet=True)
        stats = gs.parse_command("r.univar", map="e2e_omega_test.1", flags="g")
        self.assertGreater(int(stats["n"]), 0)
        self.assertGreater(float(stats["stddev"]), 0)
        self.assertGreaterEqual(float(stats["min"]), -32768)
        self.assertLessEqual(float(stats["max"]), 32767)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
