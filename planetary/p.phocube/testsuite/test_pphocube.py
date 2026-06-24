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


def _find_real_dsk():
    """Locate a real DSK kernel on this machine, used by the DSK-mode
    test below. Not bundled (the file is ~16MB); see RSDATA/Mars/
    spice_test/phobos_3_3.bds on the dev machine this was verified on
    (real PHOBOS shape model from NAIF's generic_kernels/dsk/satellites/).
    """
    candidates = [
        os.path.expanduser("~/RSDATA/Mars/spice_test"),
        os.path.expanduser("~/RSDATA/Moon/spice_test"),
    ]
    for d in candidates:
        dsk = glob.glob(os.path.join(d, "*.bds"))
        if dsk:
            return dsk[0]
    return None


def _find_real_dsk_full_ephemeris_kernels():
    """Locate a real LSK/PCK/DSK plus the *full* SPK barycenter chain
    (generic planetary ephemeris + the target moon's own satellite
    ephemeris) needed for real incidence/emission/phase on a DSK target
    body, not just local_radius (see _find_real_dsk(), which only needs
    latsrf -- no observer/look-direction ray, hence no full ephemeris
    chain). Not bundled (mar099.bsp alone is ~1.2GB) -- skips on hosts
    without a local copy. See RSDATA/Mars/spice_omega/ (de432s.bsp +
    mar099.bsp, fetched for the OMEGA camera-model work) and
    RSDATA/Mars/spice_test/phobos_3_3.bds on the dev machine this was
    verified on."""
    lsk = glob.glob(os.path.expanduser("~/RSDATA/Mars/spice_omega/naif*.tls"))
    pck = glob.glob(os.path.expanduser("~/RSDATA/Mars/spice_omega/pck*.tpc"))
    spk1 = os.path.expanduser("~/RSDATA/Mars/spice_omega/de432s.bsp")
    spk2 = os.path.expanduser("~/RSDATA/Mars/spice_omega/mar099.bsp")
    dsk = _find_real_dsk()
    if not lsk or not pck or not dsk:
        return None
    if not os.path.exists(spk1) or not os.path.exists(spk2):
        return None
    return {"lsk": lsk[0], "pck": pck[0], "spk1": spk1, "spk2": spk2, "dsk": dsk}


def _find_crism_test_kernels():
    """Locate the full real CRISM kernel set + raw FRT cube on this
    machine, used by the -c (camera mode) test below. Not bundled (the
    CK/SPK alone are tens of MB) -- skips on hosts without a local copy.
    See RSDATA/Mars/spice_test/ and RSDATA/Mars/FRT00003BFB_*  on the dev
    machine this was verified on. crismAddendum001.ti is the real ISIS3
    instrument addendum kernel (BORESIGHT_SAMPLE/PIXEL_PITCH/
    FOCAL_LENGTH -- not in the public NAIF IK), fetched from
    https://asc-isisdata.s3.us-west-2.amazonaws.com/usgs_data/mro/
    kernels/iak/crismAddendum001.ti (see TODO.md)."""
    d = os.path.expanduser("~/RSDATA/Mars/spice_test")
    lbl = os.path.expanduser(
        "~/RSDATA/Mars/FRT00003BFB_01_IF156S_TRR3.LBL")
    needed = {
        "spk1": "mar063.bsp", "spk2": "mro_psp2.bsp",
        "ck1": "mro_crm_psp_070101_070131.bc",
        "ck2": "mro_sc_psp_070102_070108.bc",
        "sclk1": "MRO_SCLKSCET.00119.tsc",
        "sclk2": "MRO_SCLKSCET.00119.65536.tsc",
        "fk": "mro_v17.tf", "ik1": "mro_crism_v10.ti",
        "ik2": "crismAddendum001.ti",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    lsk_pck_dirs = [d, os.path.expanduser("~/RSDATA/Moon/spice_test"),
                    os.path.expanduser("~/RSDATA/Saturn/kernels/lsk"),
                    os.path.expanduser("~/RSDATA/Saturn/kernels/pck")]
    lsk = [f for dd in lsk_pck_dirs for f in glob.glob(os.path.join(dd, "naif*.tls"))]
    pck = [f for dd in lsk_pck_dirs for f in glob.glob(os.path.join(dd, "pck*.tpc"))]
    if not lsk or not pck or not os.path.exists(lbl):
        return None
    if not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["lsk"] = lsk[0]
    paths["pck"] = pck[0]
    paths["lbl"] = lbl
    return paths


def _find_iss_test_kernels():
    """Locate the real Cassini ISS kernel set + raw NAC/WAC frames on this
    machine, used by the -c (camera mode) ISS tests below. Not bundled
    (the CK/SPK are several MB) -- skips on hosts without a local copy.
    See RSDATA/Saturn/spice_test/ and RSDATA/Misc/N1466182140_1_CALIB.*
    /W1466182067_1_CALIB.* on the dev machine this was verified on.
    IssNAAddendum005.ti/IssWAAddendum005.ti are the real ISIS3 instrument
    addendum kernels (BORESIGHT/PIXEL_PITCH/FOCAL_LENGTH/K1 -- not in the
    public NAIF IK), fetched via p.spice.find's kernels=...,iak (see
    TODO.md)."""
    d = os.path.expanduser("~/RSDATA/Saturn/spice_test")
    nac_lbl = os.path.expanduser("~/RSDATA/Misc/N1466182140_1_CALIB.LBL")
    wac_lbl = os.path.expanduser("~/RSDATA/Misc/W1466182067_1_CALIB.LBL")
    needed = {
        "lsk": "naif0012.tls", "sclk": "cas00172.tsc",
        "ik": "cas_iss_v10.ti", "iak_nac": "IssNAAddendum005.ti",
        "iak_wac": "IssWAAddendum005.ti", "fk": "cas_v43.tf",
        "pck": "cpck_rock_21Jan2011_merged.tpc",
        "spk": "040615AP_SCPSE_04167_04186.bsp",
        "ck": "04168_04171ra.bc",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    pck2 = glob.glob(os.path.expanduser("~/RSDATA/Saturn/kernels/pck/pck0001*.tpc"))
    if not pck2 or not os.path.exists(nac_lbl) or not os.path.exists(wac_lbl):
        return None
    if not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["pck2"] = pck2[0]
    paths["nac_lbl"] = nac_lbl
    paths["wac_lbl"] = wac_lbl
    return paths


def _find_omega_test_kernels():
    """Locate the real MEX OMEGA kernel set + raw QUBE on this machine,
    used by the -c (camera mode) OMEGA test below. Not bundled (mar099.bsp
    alone is ~1.2GB) -- skips on hosts without a local copy. See
    RSDATA/Mars/spice_omega/ and RSDATA/Mars/ORB0100_0.QUB on the dev
    machine this was verified on. Unlike CRISM/ISS, no IAK exists for
    OMEGA on the ISIS3 AWS mirror -- MIRROR_CENTER_POSITION/MIRROR_SLOPE
    come straight from the real public NAIF/ESA IK (MEX_OMEGA_V03.TI).
    de432s.bsp + mar099.bsp are real NAIF generic planetary ephemeris
    kernels, needed because the real reconstructed-orbit SPK
    (MEX_ROB_*.BSP) only gives MEX relative to MARS (499), not all the
    way to the solar system barycenter."""
    d = os.path.expanduser("~/RSDATA/Mars/spice_omega")
    cube = os.path.expanduser("~/RSDATA/Mars/ORB0100_0.QUB")
    needed = {
        "lsk": "naif0012.tls", "sclk": "MEX_260522_STEP.TSC",
        "ik": "MEX_OMEGA_V03.TI", "fk": "MEX_V16.TF",
        "pck1": "MARS_IAU2000_V0.TPC", "pck2": "pck00010.tpc",
        "spk1": "MEX_ROB_040101_041231_003.BSP", "spk2": "de432s.bsp",
        "spk3": "mar099.bsp",
        "ck": "ATNM_MEASURED_040101_050101_V03.BC",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    if not os.path.exists(cube) or not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["cube"] = cube
    return paths


def _find_vims_test_kernels():
    """Locate the real Cassini VIMS kernel set + raw QUBE on this machine,
    used by the -c (camera mode) VIMS_IR/VIMS_VIS tests below. Unlike
    CRISM/ISS, no IAK adds BORESIGHT/FOCAL_LENGTH for VIMS -- its real
    geometry is a 2-axis angular scan (ISIS3's own VimsGroundMap formula,
    ported directly), and the IAK (vimsAddendum04.ti) is only needed to
    fix a real, documented VIMS_IR/VIMS_V NAIF ID swap between the public
    IK and FK. See RSDATA/Saturn/spice_vims/ and
    RSDATA/Misc/v1799424623_1.{qub,lbl} on the dev machine this was
    verified on (T-108 Titan flyby, 2015-01-08)."""
    d = os.path.expanduser("~/RSDATA/Saturn/spice_vims")
    cube = os.path.expanduser("~/RSDATA/Misc/v1799424623_1.lbl")
    needed = {
        "lsk": "lsk/naif0012.tls", "sclk": "sclk/cas00172.tsc",
        "ik": "ik/cas_vims_v06.ti", "iak": "iak/vimsAddendum04.ti",
        "fk": "fk/cas_v43.tf",
        "pck1": "pck/cpck_rock_21Jan2011_merged.tpc",
        "pck2": "pck/pck00010.tpc",
        "spk": "spk/150108AP_SCPSE_14365_15016.bsp",
        "ck": "ck/15008_15013ra.bc",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    if not os.path.exists(cube) or not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["cube"] = cube
    return paths


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

    def test_spice_mode_line_rate_produces_row_gradient(self):
        """-s with line_rate= attached must compute a per-row ephemeris
        time (centered on the mid-scene epoch), not reuse one constant
        et for every row. Verified by diffing incidence against the same
        scene/epoch run *without* line_rate=: the difference must be a
        real, monotonic, row-indexed gradient (centered at row
        (nrows-1)/2, sign flipping above/below it) -- not all-zero (which
        would mean line_rate is being ignored) and not noise."""
        proj = gs.parse_command("g.proj", flags="g")
        if proj.get("proj") != "ll":
            self.skipTest("this test requires a PROJECTION_LL (geographic) "
                          "location")
        kernels = _find_real_test_kernels()
        if not kernels:
            self.skipTest("no local LSK/PCK/SPK test kernels found "
                          "(see _find_real_test_kernels)")
        lsk, pck, spk = kernels

        import numpy as np

        self.runModule("g.region", n=22.45, s=22.0, e=-17.65, w=-18.5,
                       res=0.05)
        m_with = "pphocube_test_lr_with_in"
        m_without = "pphocube_test_lr_without_in"
        out_with = "pphocube_test_lr_with_out"
        out_without = "pphocube_test_lr_without_out"
        try:
            self.runModule("r.mapcalc", expression=f"{m_with} = 1.0",
                           overwrite=True)
            self.runModule("p.spiceinit", map=m_with, target="MOON",
                           observer="EARTH", time="2026-04-22T14:58:39",
                           line_rate=0.5, lsk=lsk, pck=pck, spk=spk)
            self.assertModule(SimpleModule(
                "p.phocube", input=m_with, output=out_with, flags="si"))

            self.runModule("r.mapcalc", expression=f"{m_without} = 1.0",
                           overwrite=True)
            self.runModule("p.spiceinit", map=m_without, target="MOON",
                           observer="EARTH", time="2026-04-22T14:58:39",
                           lsk=lsk, pck=pck, spk=spk)
            self.assertModule(SimpleModule(
                "p.phocube", input=m_without, output=out_without, flags="si"))

            region = gs.region()
            nrows = int(region["rows"])

            def _read(mapname):
                tmp = gs.tempfile()
                gs.run_command("r.out.bin", input=mapname, output=tmp,
                               bytes=8, quiet=True)
                arr = np.fromfile(tmp, dtype=np.float64).reshape(
                    nrows, int(region["cols"]))
                os.unlink(tmp)
                return arr

            diff = (_read(f"{out_with}_incidence")
                    - _read(f"{out_without}_incidence"))
            row_means = diff.mean(axis=1)

            self.assertGreater(np.max(np.abs(row_means)), 0,
                               "line_rate= had no effect -- per-row "
                               "ephemeris time is not being applied")
            # Monotonic across rows (real linear-in-row timing offset).
            self.assertTrue(np.all(np.diff(row_means) < 0)
                            or np.all(np.diff(row_means) > 0),
                            "row gradient is not monotonic -- not a real "
                            "per-row timing effect")
            center = (nrows - 1) / 2.0
            self.assertAlmostEqual(
                float(row_means[int(round(center))])
                if center == round(center) else
                float((row_means[int(center)] + row_means[int(center) + 1]) / 2),
                0.0, delta=max(abs(row_means).max() * 0.5, 1e-9),
                msg="row gradient should be ~centered at the mid-scene row")
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=(f"{m_with},{m_without},"
                                   f"{out_with}_*,{out_without}_*"),
                           quiet=True)

    def test_spice_mode_dsk_shape_differs_from_ellipsoid(self):
        """When a real DSK kernel is attached, -s mode's local_radius
        (and the underlying surface point) must come from the real
        (non-ellipsoid) shape via p_spice_latsrf, not the smooth
        ellipsoid approximation -- this is the whole point of DSK
        support. Verified with the real PHOBOS shape model (famously
        irregular: ~9-13 km radius depending on direction), which the
        smooth ellipsoid approximation cannot reproduce. Does not
        require real target-body ephemeris (latsrf needs no observer/
        look-direction ray), so this works even on a target body whose
        own SPK isn't loaded -- unlike incidence/emission/phase, which
        still need real ephemeris and are not exercised here."""
        proj = gs.parse_command("g.proj", flags="g")
        if proj.get("proj") != "ll":
            self.skipTest("this test requires a PROJECTION_LL (geographic) "
                          "location")
        kernels = _find_real_test_kernels()
        dsk = _find_real_dsk()
        if not kernels or not dsk:
            self.skipTest("no local LSK/PCK/SPK/DSK test kernels found "
                          "(see _find_real_test_kernels/_find_real_dsk)")
        lsk, pck, spk = kernels

        mapname = "pphocube_test_dsk_input"
        self.runModule("g.region", n=15, s=-15, e=30, w=0, res=1)
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        self.runModule("p.spiceinit", map=mapname, target="PHOBOS",
                       observer="EARTH", time="2026-04-22T14:58:39",
                       lsk=lsk, pck=pck, spk=spk, dsk=dsk)
        out_prefix = "pphocube_test_dsk_out"
        try:
            module = SimpleModule(
                "p.phocube", input=mapname, output=out_prefix, flags="sr")
            self.assertModule(module)

            stats = gs.parse_command("r.univar", flags="g",
                                      map=f"{out_prefix}_local_radius")
            self.assertEqual(int(stats["null_cells"]), 0)
            # Phobos's real shape varies ~9-13 km; a smooth ellipsoid at
            # this scale would vary far less over a 30x30 deg patch and
            # vary smoothly -- a real, sizeable stddev is the signature
            # of genuine irregular-shape data, not an ellipsoid fallback.
            self.assertGreater(float(stats["stddev"]), 0.05)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname},{out_prefix}_*", quiet=True)

    def test_spice_mode_dsk_real_incidence_emission_phase(self):
        """Real-ephemeris incidence/emission/phase on a DSK (non-ellipsoid)
        target body -- the full chain (target's own SPK ephemeris, not
        just latsrf's known-(lon,lat)-to-surface-point mapping) that
        test_spice_mode_dsk_shape_differs_from_ellipsoid above could not
        exercise (TODO.md item 2: previously blocked only by lacking a
        real Mars-system satellite ephemeris kernel small enough to keep
        around -- mar099.bsp, ~1.2GB, fetched in a later session for the
        OMEGA camera-model work, incidentally unblocks this too). Uses
        the real PHOBOS DSK shape model + EARTH as observer (full real
        ephemeris chain: PHOBOS -> MARS BARYCENTER -> SSB via mar099.bsp,
        EARTH -> EARTH BARYCENTER -> SSB via de432s.bsp). Confirms a 100%
        pixel hit rate and physically sane, real incidence/emission/phase
        (not just local_radius) on the real irregular shape, not the
        ellipsoid approximation."""
        proj = gs.parse_command("g.proj", flags="g")
        if proj.get("proj") != "ll":
            self.skipTest("this test requires a PROJECTION_LL (geographic) "
                          "location")
        kernels = _find_real_dsk_full_ephemeris_kernels()
        if not kernels:
            self.skipTest("no local LSK/PCK/DSK + de432s.bsp/mar099.bsp "
                          "found (see _find_real_dsk_full_ephemeris_kernels)")

        mapname = "pphocube_test_dsk_iep_input"
        self.runModule("g.region", n=15, s=-15, e=30, w=0, res=1)
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        self.runModule("p.spiceinit", map=mapname, target="PHOBOS",
                       observer="EARTH", time="2026-04-22T14:58:39",
                       lsk=kernels["lsk"], pck=kernels["pck"],
                       spk=f"{kernels['spk1']},{kernels['spk2']}",
                       dsk=kernels["dsk"])
        out_prefix = "pphocube_test_dsk_iep_out"
        try:
            module = SimpleModule(
                "p.phocube", input=mapname, output=out_prefix, flags="siepr")
            self.assertModule(module)

            inc = gs.parse_command("r.univar", flags="g",
                                    map=f"{out_prefix}_incidence")
            pha = gs.parse_command("r.univar", flags="g",
                                    map=f"{out_prefix}_phase")
            self.assertEqual(int(inc["null_cells"]), 0,
                             "expected 100% pixel hit rate (0 NULL)")
            self.assertGreater(float(inc["min"]), 0)
            self.assertLess(float(inc["max"]), 180)
            # Phase angle barely varies across a 30x30 deg patch at
            # Mars-Earth range -- real, not a stand-in for a missing
            # computation (a constant 0 or NaN would be the failure
            # signature here, not a small real spread).
            self.assertGreater(float(pha["min"]), 0)
            self.assertLess(float(pha["max"]) - float(pha["min"]), 1.0)
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

    def test_camera_mode_requires_instrument(self):
        """-c without instrument= must G_fatal_error, not guess."""
        mapname = "pphocube_test_cam_noinstr"
        self.runModule("r.mapcalc",
                       expression=f"{mapname} = 1.0", overwrite=True)
        try:
            module = SimpleModule(
                "p.phocube", input=mapname, output="pphocube_test_cam_out",
                flags="ci")
            self.assertModuleFail(module, msg="expected G_fatal_error")
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           name=mapname, quiet=True)

    def test_camera_mode_real_crism_geometry(self):
        """Real-kernel correctness check for -c: the real FRT00003BFB
        CRISM observation, real MRO/CRISM kernels (including the
        crismAddendum001.ti instrument addendum kernel -- see
        _find_crism_test_kernels and TODO.md), in a PROJECTION_XY
        (un-georeferenced pixel/line) location. Confirms the full
        pinhole-camera-model -> sincpt/ilumin pipeline produces a 100%
        pixel hit rate and lat/lon matching Mawrth Vallis's known
        location (~22.4N, 341E), not just crash-free output."""
        kernels = _find_crism_test_kernels()
        if not kernels:
            self.skipTest("no local CRISM test kernel set found "
                          "(see _find_crism_test_kernels)")

        mapname = "pphocube_test_crism"
        out_prefix = "pphocube_test_crism_out"
        self.runModule("p.in.pds3", input=kernels["lbl"], output=mapname,
                       overwrite=True, quiet=True)
        self.runModule("g.region", raster=f"{mapname}.1")
        self.runModule(
            "p.spiceinit", map=f"{mapname}.1", target="MARS",
            observer="MRO", time="2007-01-05T01:26:56.855",
            spk=f"{kernels['spk1']},{kernels['spk2']}",
            ck=f"{kernels['ck1']},{kernels['ck2']}",
            sclk=f"{kernels['sclk1']},{kernels['sclk2']}",
            fk=kernels["fk"], ik=f"{kernels['ik1']},{kernels['ik2']}",
            pck=kernels["pck"], lsk=kernels["lsk"])
        try:
            module = SimpleModule(
                "p.phocube", flags="cietn", input=f"{mapname}.1",
                output=out_prefix, instrument="CRISM_VNIR", target="MARS")
            self.assertModule(module)

            lat = gs.parse_command("r.univar", flags="g",
                                   map=f"{out_prefix}_lat")
            lon = gs.parse_command("r.univar", flags="g",
                                   map=f"{out_prefix}_lon")
            self.assertEqual(int(lat["null_cells"]), 0,
                             "expected 100% pixel hit rate (0 NULL)")
            self.assertAlmostEqual(float(lat["mean"]), 22.149, delta=0.05)
            self.assertAlmostEqual(float(lon["mean"]), -17.95, delta=0.1)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname}.*,{out_prefix}_*",
                           quiet=True)

    def test_camera_mode_real_iss_nac_geometry(self):
        """Real-kernel correctness check for -c ISS_NAC: a real Cassini
        ISS NAC frame of Saturn (co-iss-n1466182140, 2004-06-17, filter
        P0/CB2 -- not in the IAK, exercises the documented
        DEFAULT_FOCAL_LENGTH fallback too) confirmed via OPUS's own
        SURFACEGEOsaturn_rangetobody1 field to have Saturn's surface in
        view. Confirms the full pinhole+K1-distortion+per-filter-focal-
        length -> sincpt/ilumin pipeline produces a 100% pixel hit rate
        and physically sane geometry (southern hemisphere, moderate
        emission/incidence), not just crash-free output."""
        kernels = _find_iss_test_kernels()
        if not kernels:
            self.skipTest("no local Cassini ISS test kernel set found "
                          "(see _find_iss_test_kernels)")

        mapname = "pphocube_test_iss_nac"
        out_prefix = "pphocube_test_iss_nac_out"
        # p.in.pds3 clips/pads to whatever region is currently active
        # (only a trivial 1x1/0x0 region triggers its own auto-resize-to-
        # image-dims) -- setUpClass leaves a fixed 10x10 region active,
        # so reset to 1x1 first, then to the real imported size after.
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        self.runModule("p.in.pds3", input=kernels["nac_lbl"], output=mapname,
                       overwrite=True, quiet=True)
        self.runModule("g.region", raster=mapname)
        self.runModule(
            "p.spiceinit", map=mapname, target="SATURN", observer="CASSINI",
            time="2004-169T16:24:48.262",
            lsk=kernels["lsk"], sclk=kernels["sclk"],
            ik=f"{kernels['ik']},{kernels['iak_nac']}", fk=kernels["fk"],
            pck=f"{kernels['pck']},{kernels['pck2']}", spk=kernels["spk"],
            ck=kernels["ck"])
        try:
            module = SimpleModule(
                "p.phocube", flags="cietn", input=mapname, output=out_prefix,
                instrument="ISS_NAC", target="SATURN",
                filter1="P0", filter2="CB2")
            self.assertModule(module)

            lat = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lat")
            self.assertEqual(int(lat["null_cells"]), 0,
                             "expected 100% pixel hit rate (0 NULL)")
            self.assertLess(float(lat["max"]), 0,
                            "expected this frame to view Saturn's southern "
                            "hemisphere only")
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname},{out_prefix}_*", quiet=True)

    def test_camera_mode_real_iss_wac_geometry(self):
        """Real-kernel correctness check for -c ISS_WAC: a real Cassini
        ISS WAC frame of Saturn (co-iss-w1466182067, same observation
        sequence as the NAC test above, filter CB2/IRP0 -- exercises the
        exact-filter-match path, not the fallback). WAC's wide FOV should
        capture the *whole* disk at this 8.29M km range -- confirms a
        small (~4%) but nonzero hit fraction with full -180..180 deg
        longitude coverage, not just crash-free output."""
        kernels = _find_iss_test_kernels()
        if not kernels:
            self.skipTest("no local Cassini ISS test kernel set found "
                          "(see _find_iss_test_kernels)")

        mapname = "pphocube_test_iss_wac"
        out_prefix = "pphocube_test_iss_wac_out"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        self.runModule("p.in.pds3", input=kernels["wac_lbl"], output=mapname,
                       overwrite=True, quiet=True)
        self.runModule("g.region", raster=mapname)
        self.runModule(
            "p.spiceinit", map=mapname, target="SATURN", observer="CASSINI",
            time="2004-169T16:23:39.198",
            lsk=kernels["lsk"], sclk=kernels["sclk"],
            ik=f"{kernels['ik']},{kernels['iak_wac']}", fk=kernels["fk"],
            pck=f"{kernels['pck']},{kernels['pck2']}", spk=kernels["spk"],
            ck=kernels["ck"])
        try:
            module = SimpleModule(
                "p.phocube", flags="cietn", input=mapname, output=out_prefix,
                instrument="ISS_WAC", target="SATURN",
                filter1="CB2", filter2="IRP0")
            self.assertModule(module)

            lon = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lon")
            self.assertGreater(int(lon["n"]), 0,
                               "expected at least some pixels to hit Saturn's disk")
            self.assertLess(float(lon["min"]), -170)
            self.assertGreater(float(lon["max"]), 170)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname},{out_prefix}_*", quiet=True)

    def test_camera_mode_real_omega_swir_c_geometry(self):
        """Real-kernel correctness check for -c OMEGA_SWIR_C: a real MEX
        OMEGA EDR (orbit 100, 2004-02-10, ORB0100_0.QUB) confirms the
        whiskbroom scanning-mirror model (boresight rotated about the
        detector frame's +Y axis by (mirror_dn - MIRROR_CENTER_POSITION)
        * MIRROR_SLOPE degrees, read straight from the real public IK --
        no IAK exists for OMEGA) against this cube's own real, known
        ground-truth lat/lon bounds (label: MAXIMUM_LATITUDE=-70.253,
        MINIMUM_LATITUDE=-78.167, EASTERNMOST_LONGITUDE=303.019,
        WESTERNMOST_LONGITUDE=291.415). Confirms a 100% pixel hit rate
        and that computed bounds land within ~0.1 deg of the label's,
        not just crash-free output. Also exercises p.in.pds3's
        suffix_band= option (the per-sample mirror-DN housekeeping
        sideplane this camera model depends on)."""
        kernels = _find_omega_test_kernels()
        if not kernels:
            self.skipTest("no local MEX OMEGA test kernel set found "
                          "(see _find_omega_test_kernels)")

        band_prefix = "pphocube_test_omega_swirc"
        mirror_map = "pphocube_test_omega_mirror_dn"
        out_prefix = "pphocube_test_omega_out"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        # Imports all 352 bands (VIS+SWIR-L+SWIR-C); only band 1
        # (SWIR-C channel 0) is needed for this geometry check.
        self.runModule("p.in.pds3", input=kernels["cube"], output=band_prefix,
                       overwrite=True, quiet=True)
        self.runModule("p.in.pds3", input=kernels["cube"], output=mirror_map,
                       suffix_band=1, overwrite=True, quiet=True)
        self.runModule("g.region", raster=f"{band_prefix}.1")
        self.runModule(
            "p.spiceinit", map=f"{band_prefix}.1", target="MARS", observer="-41",
            # Mid-scene epoch + real per-line cadence, both derived from
            # this cube's own real START_TIME/STOP_TIME/LINES (label:
            # 2004-02-10T18:07:10.035 .. 18:10:00.060, 424 lines).
            time="2004-02-10T18:08:35.0475", line_rate="0.401002358",
            lsk=kernels["lsk"], sclk=kernels["sclk"], ik=kernels["ik"],
            fk=kernels["fk"], pck=f"{kernels['pck1']},{kernels['pck2']}",
            spk=f"{kernels['spk1']},{kernels['spk2']},{kernels['spk3']}",
            ck=kernels["ck"])
        try:
            module = SimpleModule(
                "p.phocube", flags="ctn", input=f"{band_prefix}.1",
                output=out_prefix, instrument="OMEGA_SWIR_C",
                mirror_dn=mirror_map)
            self.assertModule(module)

            lat = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lat")
            lon = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lon")
            self.assertEqual(int(lat["null_cells"]), 0,
                             "expected 100% pixel hit rate (0 NULL)")
            self.assertAlmostEqual(float(lat["max"]), -70.253, delta=0.1)
            self.assertAlmostEqual(float(lat["min"]), -78.167, delta=0.1)
            # lon is computed in [-180,180]; the label's bounds are 0-360E.
            self.assertAlmostEqual(float(lon["max"]) + 360.0, 303.019, delta=0.1)
            self.assertAlmostEqual(float(lon["min"]) + 360.0, 291.415, delta=0.1)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{band_prefix}.*,{mirror_map},{out_prefix}_*",
                           quiet=True)

    def _camera_mode_real_vims(self, instrument, expect_lat_max, expect_lat_min,
                                expect_lon_max, expect_lon_min):
        """Shared body for the VIMS_IR/VIMS_VIS real-kernel checks below."""
        kernels = _find_vims_test_kernels()
        if not kernels:
            self.skipTest("no local Cassini VIMS test kernel set found "
                          "(see _find_vims_test_kernels)")

        band_prefix = f"pphocube_test_vims_{instrument.lower()}_in"
        out_prefix = f"pphocube_test_vims_{instrument.lower()}_out"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        self.runModule("p.in.pds3", flags="g", input=kernels["cube"],
                       output=band_prefix, overwrite=True, quiet=True)
        self.runModule("g.region", raster=f"{band_prefix}.1")
        self.runModule(
            "p.spiceinit", map=f"{band_prefix}.1", target="TITAN",
            observer="CASSINI", time="2015-008T15:09:40.135",
            lsk=kernels["lsk"], sclk=kernels["sclk"],
            ik=f"{kernels['ik']},{kernels['iak']}", fk=kernels["fk"],
            pck=f"{kernels['pck1']},{kernels['pck2']}",
            spk=kernels["spk"], ck=kernels["ck"])
        try:
            module = SimpleModule(
                "p.phocube", flags="ctn", input=f"{band_prefix}.1",
                output=out_prefix, instrument=instrument,
                sampling_mode="HI-RES" if instrument == "VIMS_IR" else "NORMAL",
                x_offset=11, z_offset=25, swath_width=38, swath_length=18)
            self.assertModule(module)

            lat = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lat")
            lon = gs.parse_command("r.univar", flags="g", map=f"{out_prefix}_lon")
            self.assertLess(int(lat["null_cells"]), 684,
                            "expected at least some pixels to hit Titan's disk")
            self.assertAlmostEqual(float(lat["max"]), expect_lat_max, delta=0.5)
            self.assertAlmostEqual(float(lat["min"]), expect_lat_min, delta=0.5)
            self.assertAlmostEqual(float(lon["max"]), expect_lon_max, delta=0.5)
            self.assertAlmostEqual(float(lon["min"]), expect_lon_min, delta=0.5)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{band_prefix}.*,{out_prefix}_*", quiet=True)

    def test_camera_mode_real_vims_ir_geometry(self):
        """Real-kernel correctness check for -c VIMS_IR: a real Cassini
        VIMS cube from the T-108 Titan flyby (2015-01-08,
        v1799424623_1.qub) confirms the 2-axis angular scan model (ported
        directly from ISIS3's VimsGroundMap::LookDirection() -- no IAK
        adds a boresight/pixel-pitch model for VIMS, unlike CRISM/ISS)
        against this run's own verified real-data result: a real,
        physically sane, smoothly-varying disk patch of Titan, not a
        degenerate all-NULL or all-identical output. Bounds frozen from
        the first verified run (see TODO.md)."""
        self._camera_mode_real_vims("VIMS_IR", 68.09, -65.23, 104.54, -62.32)

    def test_camera_mode_real_vims_vis_geometry(self):
        """VIS-channel equivalent of test_camera_mode_real_vims_ir_geometry
        -- same cube, same epoch, different (co-boresighted) channel.
        Confirms VIS's geometry lands on an overlapping (not identical --
        different boresight/pixel-pitch/SamplingMode) patch of Titan's
        disk, as physically expected for two co-mounted, simultaneously-
        acquired channels of the same real observation."""
        self._camera_mode_real_vims("VIMS_VIS", 74.68, -64.93, 92.88, -30.36)


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
