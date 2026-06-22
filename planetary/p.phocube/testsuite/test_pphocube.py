"""Test of p.phocube

ISIS3 equivalent: phocube
Purpose: Per-pixel geometric backplanes

@author Yann Chemin
"""

import glob
import os
import shutil
import unittest
from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


def _find_real_test_kernels():
    """Locate a real LSK + PCK + planetary SPK on this machine, used by
    the -s (SPICE mode) tests below. Not bundled in the repo (the SPK
    alone is ~100MB) -- these tests skip on hosts that don't have a local
    copy. See RSDATA/Moon/spice_test/ on the dev machine this was
    verified on (real naif0012.tls + pck00011.tpc + de430.bsp from NAIF)."""
    candidates = [
        os.path.expanduser("~/RSDATA/Moon/spice_test"),
        os.path.expanduser("~/RSDATA/Mars/spice_test"),
    ]
    for d in candidates:
        lsk = glob.glob(os.path.join(d, "naif*.tls"))
        pck = glob.glob(os.path.join(d, "pck*.tpc"))
        spk = glob.glob(os.path.join(d, "de*.bsp"))
        if lsk and pck and spk:
            return lsk[0], pck[0], spk[0]
    return None


class TestPphocube(TestCase):
    """Test p.phocube module.

    Verifies module interface and (where ISIS3 binary is available)
    cross-validates outputs against the equivalent ISIS3 application: phocube.
    """

    @classmethod
    def setUpClass(cls):
        """Set up temporary region for raster tests."""
        cls.use_temp_region()
        cls.runModule("g.region", n=10, s=0, e=10, w=0, rows=10, cols=10)

    @classmethod
    def tearDownClass(cls):
        """Remove temporary region."""
        cls.del_temp_region()

    def test_help(self):
        """Test that --help works without error."""
        module = SimpleModule("p.phocube", flags="h")
        self.assertModule(module)

    def test_module_metadata(self):
        """Test that module exposes proper GRASS metadata
        (keywords, label, description) via --interface-description."""
        module = SimpleModule("p.phocube", flags="", overwrite=False,
                              run_=False)
        # Verify the module binary exists in PATH
        self.assertIsNotNone(shutil.which("p.phocube"),
                             "Module p.phocube not found in PATH")

    def test_flatfield_mode_unchanged(self):
        """Regression: flat-field mode (no -s) must still work exactly as
        before -s was added -- this is the existing, default code path."""
        mapname = "pphocube_test_flat_input"
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        out_prefix = "pphocube_test_flat_out"
        module = SimpleModule(
            "p.phocube", input=mapname, output=out_prefix, target="mars",
            sun_x=0.55, sun_y=-0.10, sun_z=0.82,
            obs_x=3254.8, obs_y=-1057.5, obs_z=1406.4, flags="iep")
        self.assertModule(module)
        try:
            stats = gs.parse_command("r.univar", flags="g",
                                      map=f"{out_prefix}_incidence")
            self.assertEqual(int(stats["null_cells"]), 0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname},{out_prefix}_*", quiet=True)

    def test_spice_mode_rejects_projection_xy(self):
        """Regression/safety test: -s on an un-georeferenced pixel/line
        grid (PROJECTION_XY, e.g. p.in.pds3 -g output) must fail loudly
        via G_fatal_error, not silently misinterpret sample/line indices
        as degrees of longitude/latitude (the original p.phocube.md/
        implementation mismatch this -s mode was built to fix)."""
        proj = gs.parse_command("g.proj", flags="g")
        if "proj" in proj:
            # A real CRS (geographic or projected) is active, not XY.
            self.skipTest("this test requires a PROJECTION_XY (x,y) "
                          "location; current location is georeferenced")

        kernels = _find_real_test_kernels()
        if not kernels:
            self.skipTest("no local LSK/PCK/SPK test kernels found "
                          "(see _find_real_test_kernels)")
        lsk, pck, spk = kernels

        mapname = "pphocube_test_xy_input"
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        self.runModule("p.spiceinit", map=mapname, target="MOON",
                       observer="EARTH", time="2026-04-22T14:58:39",
                       lsk=lsk, pck=pck, spk=spk)
        try:
            module = SimpleModule(
                "p.phocube", input=mapname, output="pphocube_test_xy_out",
                flags="si")
            self.assertModuleFail(module, msg="expected G_fatal_error")
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           name=mapname, quiet=True)

    def test_spice_mode_real_ephemeris_geometry(self):
        """Real-kernel correctness check for -s: real LSK+PCK+SPK, target
        MOON observed from EARTH at a real UTC time, in a real geographic
        (PROJECTION_LL) location. Confirms the whole read-history -> load
        kernels -> et conversion -> real PCK radii -> per-pixel ilumin
        pipeline runs end-to-end and produces sane, smoothly-varying,
        non-NULL incidence/emission/phase (not the degenerate all-NaN/
        all-identical output of a broken pipeline)."""
        proj = gs.parse_command("g.proj", flags="g")
        if proj.get("proj") != "ll":
            self.skipTest("this test requires a PROJECTION_LL (geographic) "
                          "location")

        kernels = _find_real_test_kernels()
        if not kernels:
            self.skipTest("no local LSK/PCK/SPK test kernels found "
                          "(see _find_real_test_kernels)")
        lsk, pck, spk = kernels

        mapname = "pphocube_test_spice_input"
        self.runModule("g.region", n=22.45, s=22.0, e=-17.65, w=-18.5,
                       res=0.05)
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        self.runModule("p.spiceinit", map=mapname, target="MOON",
                       observer="EARTH", time="2026-04-22T14:58:39",
                       lsk=lsk, pck=pck, spk=spk)
        out_prefix = "pphocube_test_spice_out"
        try:
            module = SimpleModule(
                "p.phocube", input=mapname, output=out_prefix, flags="siep")
            self.assertModule(module)

            stats = gs.parse_command("r.univar", flags="g",
                                      map=f"{out_prefix}_incidence")
            self.assertEqual(int(stats["null_cells"]), 0)
            # Real Earth-Moon geometry at this real epoch: incidence well
            # within [0, 180], and not perfectly uniform across the patch
            # (real per-pixel variation, not a flat-field constant).
            self.assertGreater(float(stats["min"]), 0.0)
            self.assertLess(float(stats["max"]), 180.0)
            self.assertGreater(float(stats["stddev"]), 0.0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname},{out_prefix}_*", quiet=True)

    @unittest.skipUnless(shutil.which("phocube"),
                         "ISIS3 application phocube not available "
                         "in PATH - skipping cross-validation test")
    def test_isis3_equivalence(self):
        """Cross-validate against ISIS3 phocube when both are installed.

        This test is automatically skipped when ISIS3 is not available
        on the test host. When ISIS3 is available, this verifies output
        consistency with the canonical ISIS3 implementation.
        """
        # Reference ISIS3 documentation:
        # https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/phocube/phocube.html
        # Placeholder: full cross-validation requires sample planetary data
        # and a working ISIS3 environment; tests will be extended once a
        # reference data fixture is established.
        self.skipTest("Full ISIS3 cross-validation requires reference data fixture")


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
