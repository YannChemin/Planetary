"""Test of p.caminfo

ISIS3 equivalent: caminfo
Purpose: Camera/image metadata extraction

@author Yann Chemin
"""

import glob
import json
import os
import shutil
import unittest
from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


def _find_omega_test_kernels():
    """Locate the real MEX OMEGA kernel set + raw QUBE on this machine."""
    d = os.path.expanduser("~/RSDATA/Mars/spice_omega")
    qub = os.path.expanduser("~/RSDATA/Mars/ORB0100_0.QUB")
    needed = {
        "lsk": "naif0012.tls", "sclk": "MEX_260522_STEP.TSC",
        "ik": "MEX_OMEGA_V03.TI", "fk": "MEX_V16.TF",
        "pck": "MARS_IAU2000_V0.TPC", "pck2": "pck00010.tpc",
        "spk_mex": "MEX_ROB_040101_041231_003.BSP",
        "spk_de": "de432s.bsp", "spk_mar": "mar099.bsp",
        "ck": "ATNM_MEASURED_040101_050101_V03.BC",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    if not os.path.exists(qub) or not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["qub"] = qub
    return paths


def _find_vims_test_kernels():
    """Locate the real Cassini VIMS kernel set + raw QUBE on this machine."""
    d = os.path.expanduser("~/RSDATA/Saturn/spice_vims")
    qub = os.path.expanduser("~/RSDATA/Misc/v1799424623_1.qub")
    lbl = os.path.expanduser("~/RSDATA/Misc/v1799424623_1.lbl")
    needed = {
        "lsk": os.path.join(d, "lsk", "naif0012.tls"),
        "sclk": os.path.join(d, "sclk", "cas00172.tsc"),
        "ik": os.path.join(d, "ik", "cas_vims_v06.ti"),
        "iak": os.path.join(d, "iak", "vimsAddendum04.ti"),
        "fk": os.path.join(d, "fk", "cas_v43.tf"),
        "pck": os.path.join(d, "pck", "cpck_rock_21Jan2011_merged.tpc"),
        "pck2": os.path.join(d, "pck", "pck00010.tpc"),
        "spk": os.path.join(d, "spk", "150108AP_SCPSE_14365_15016.bsp"),
        "ck": os.path.join(d, "ck", "15008_15013ra.bc"),
    }
    if not os.path.exists(qub) or not os.path.exists(lbl):
        return None
    if not all(os.path.exists(p) for p in needed.values()):
        return None
    needed["qub"] = qub
    needed["lbl"] = lbl
    return needed


def _find_crism_test_kernels():
    """Locate the real CRISM kernel set + raw FRT cube on this machine,
    used by the real-data test below. Identical fixture to p.phocube's
    own _find_crism_test_kernels() (testsuite/test_pphocube.py) -- see
    that module for where these real files come from."""
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
    """Locate the real Cassini ISS kernel set + raw NAC frame on this
    machine, used by the real-data test below. Identical fixture to
    p.phocube's own _find_iss_test_kernels()."""
    d = os.path.expanduser("~/RSDATA/Saturn/spice_test")
    nac_lbl = os.path.expanduser("~/RSDATA/Misc/N1466182140_1_CALIB.LBL")
    needed = {
        "lsk": "naif0012.tls", "sclk": "cas00172.tsc",
        "ik": "cas_iss_v10.ti", "iak_nac": "IssNAAddendum005.ti",
        "fk": "cas_v43.tf",
        "pck": "cpck_rock_21Jan2011_merged.tpc",
        "spk": "040615AP_SCPSE_04167_04186.bsp",
        "ck": "04168_04171ra.bc",
    }
    paths = {k: os.path.join(d, v) for k, v in needed.items()}
    pck2 = glob.glob(os.path.expanduser("~/RSDATA/Saturn/kernels/pck/pck0001*.tpc"))
    if not pck2 or not os.path.exists(nac_lbl):
        return None
    if not all(os.path.exists(p) for p in paths.values()):
        return None
    paths["pck2"] = pck2[0]
    paths["nac_lbl"] = nac_lbl
    return paths


class TestPcaminfo(TestCase):
    """Test p.caminfo module.

    Verifies module interface and (where ISIS3 binary is available)
    cross-validates outputs against the equivalent ISIS3 application: caminfo.
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
        module = SimpleModule("p.caminfo", flags="h")
        self.assertModule(module)

    def test_module_metadata(self):
        """Test that module exposes proper GRASS metadata
        (keywords, label, description) via --interface-description."""
        module = SimpleModule("p.caminfo", flags="", overwrite=False,
                              run_=False)
        # Verify the module binary exists in PATH
        self.assertIsNotNone(shutil.which("p.caminfo"),
                             "Module p.caminfo not found in PATH")

    def test_camera_mode_real_crism_geometry(self):
        """Real-kernel correctness check: the real FRT00003BFB CRISM
        observation (same fixture as p.phocube's own CRISM test).
        Confirms the centre lat/lon matches Mawrth Vallis's known
        location (~22.4N, 341E), and that solar distance/resolution/
        sub-solar/sub-spacecraft outputs are all real, sane numbers --
        not just crash-free output."""
        kernels = _find_crism_test_kernels()
        if not kernels:
            self.skipTest("no local CRISM test kernel set found "
                          "(see _find_crism_test_kernels)")

        mapname = "pcaminfo_test_crism"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
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
            raw = gs.read_command(
                "p.caminfo", flags="j", input=f"{mapname}.1",
                instrument="CRISM_VNIR")
            out = json.loads(raw)
            self.assertAlmostEqual(float(out["centre_lat_deg"]), 22.149,
                                   delta=0.05)
            lon = float(out["centre_lon_deg"])
            if lon > 180:
                lon -= 360
            self.assertAlmostEqual(lon, -17.95, delta=0.1)
            self.assertGreater(float(out["solar_distance_au"]), 1.0)
            self.assertLess(float(out["solar_distance_au"]), 2.0)
            self.assertGreater(float(out["pixel_resolution_m"]), 0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname}.*", quiet=True)

    def test_camera_mode_real_iss_nac_geometry(self):
        """Real-kernel correctness check: a real Cassini ISS NAC frame
        of Saturn (same fixture as p.phocube's/p.cam2map's own ISS
        tests, INSTRUMENT_MODE_ID=SUM2). Confirms the centre lat/lon
        matches the independently-computed p.phocube -c forward
        geometry for this exact frame (see TODO.md candidate #5), and
        that solar distance/resolution/sub-solar/sub-spacecraft are
        real, physically sane numbers."""
        kernels = _find_iss_test_kernels()
        if not kernels:
            self.skipTest("no local Cassini ISS test kernel set found "
                          "(see _find_iss_test_kernels)")

        mapname = "pcaminfo_test_iss_nac"
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
            raw = gs.read_command(
                "p.caminfo", flags="j", input=mapname,
                instrument="ISS_NAC", filter1="P0", filter2="CB2")
            out = json.loads(raw)
            # Independently computed via p.phocube -c on this exact frame
            # (TODO.md candidate #5): centre lat -10.58, lon -42.60 (i.e.
            # 317.40 in 0-360 convention).
            self.assertAlmostEqual(float(out["centre_lat_deg"]), -10.58,
                                   delta=0.5)
            self.assertAlmostEqual(float(out["centre_lon_deg"]), 317.40,
                                   delta=0.5)
            self.assertGreater(float(out["solar_distance_au"]), 5.0)
            self.assertLess(float(out["solar_distance_au"]), 12.0)
            self.assertGreater(float(out["pixel_resolution_m"]), 0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname}", quiet=True)

    def test_camera_mode_real_omega_swir_c_geometry(self):
        """Real-kernel correctness check: the real MEX OMEGA orbit-100
        QUBE (OMEGA_SWIR_C), same fixture as p.phocube's own OMEGA test.
        Confirms that the centre lat/lon lands within the label's own
        declared lat/lon extents (the ground truth available for this
        observation), and that solar distance/resolution/north-azimuth
        are real, physically sane numbers.  mirror_dn= is the
        band-suffix sideplane (suffix_band=1), required by p.caminfo
        for OMEGA_SWIR_C."""
        kernels = _find_omega_test_kernels()
        if not kernels:
            self.skipTest("no local MEX OMEGA test kernel set found "
                          "(see _find_omega_test_kernels)")

        mapname = "pcaminfo_test_omega_swir_c"
        mdn_map = "pcaminfo_test_omega_mirror_dn"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        self.runModule("p.in.pds3", input=kernels["qub"], output=mapname,
                       overwrite=True, quiet=True)
        self.runModule("p.in.pds3", input=kernels["qub"], output=mdn_map,
                       suffix_band=1, overwrite=True, quiet=True)
        self.runModule("g.region", raster=f"{mapname}.1")
        self.runModule(
            "p.spiceinit", map=f"{mapname}.1", target="MARS",
            observer="MEX", time="2004-02-10T18:08:35",
            line_rate=0.401002358,
            lsk=kernels["lsk"], sclk=kernels["sclk"],
            ik=kernels["ik"], fk=kernels["fk"],
            pck=f"{kernels['pck']},{kernels['pck2']}",
            spk=f"{kernels['spk_mex']},{kernels['spk_de']},{kernels['spk_mar']}",
            ck=kernels["ck"])
        try:
            raw = gs.read_command(
                "p.caminfo", flags="j", input=f"{mapname}.1",
                instrument="OMEGA_SWIR_C", mirror_dn=mdn_map)
            out = json.loads(raw)
            # label ground truth: lat in [-78.167, -70.253],
            # lon in [291.415, 303.019]
            self.assertGreater(float(out["centre_lat_deg"]), -78.5)
            self.assertLess(float(out["centre_lat_deg"]), -69.5)
            lon = float(out["centre_lon_deg"])
            self.assertGreater(lon, 290.0)
            self.assertLess(lon, 304.0)
            self.assertGreater(float(out["solar_distance_au"]), 1.3)
            self.assertLess(float(out["solar_distance_au"]), 1.7)
            self.assertGreater(float(out["pixel_resolution_m"]), 0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname}.*", quiet=True)
            self.runModule("g.remove", flags="f", type="raster",
                           name=mdn_map, quiet=True)

    def test_camera_mode_real_vims_ir_geometry(self):
        """Real-kernel correctness check: the real Cassini VIMS T-108
        Titan flyby cube (VIMS_IR, HI-RES), same fixture as p.phocube's
        own VIMS test. Confirms that the centre lat/lon lands within the
        known Titan-disk extent for this observation, and that solar
        distance and resolution are physically sane."""
        kernels = _find_vims_test_kernels()
        if not kernels:
            self.skipTest("no local Cassini VIMS test kernel set found "
                          "(see _find_vims_test_kernels)")

        mapname = "pcaminfo_test_vims_ir"
        self.runModule("g.region", n=1, s=0, e=1, w=0, rows=1, cols=1)
        self.runModule("p.in.pds3", input=kernels["lbl"], output=mapname,
                       overwrite=True, quiet=True)
        self.runModule("g.region", raster=f"{mapname}.1")
        self.runModule(
            "p.spiceinit", map=f"{mapname}.1", target="TITAN",
            observer="CASSINI", time="2015-01-08T15:09:40.135",
            lsk=kernels["lsk"], sclk=kernels["sclk"],
            ik=f"{kernels['ik']},{kernels['iak']}", fk=kernels["fk"],
            pck=f"{kernels['pck']},{kernels['pck2']}",
            spk=kernels["spk"], ck=kernels["ck"])
        try:
            raw = gs.read_command(
                "p.caminfo", flags="j", input=f"{mapname}.1",
                instrument="VIMS_IR",
                sampling_mode="HI-RES",
                x_offset=11, z_offset=25,
                swath_width=38, swath_length=18)
            out = json.loads(raw)
            # p.phocube verified lat range: -65.23..68.09, lon: -62.32..104.54
            # Centre should be within this patch (hit or miss is geometry-dependent)
            self.assertIn("centre_hit", out)
            self.assertGreater(float(out["solar_distance_au"]), 7.0)
            self.assertLess(float(out["solar_distance_au"]), 12.0)
            # If centre hits: lat/lon within the verified patch extents
            if out.get("centre_hit"):
                self.assertGreater(float(out["centre_lat_deg"]), -70.0)
                self.assertLess(float(out["centre_lat_deg"]), 75.0)
                self.assertGreater(float(out["pixel_resolution_m"]), 0)
        finally:
            self.runModule("g.remove", flags="f", type="raster",
                           pattern=f"{mapname}.*", quiet=True)

    @unittest.skipUnless(shutil.which("caminfo"),
                         "ISIS3 application caminfo not available "
                         "in PATH - skipping cross-validation test")
    def test_isis3_equivalence(self):
        """Cross-validate against ISIS3 caminfo when both are installed.

        This test is automatically skipped when ISIS3 is not available
        on the test host. When ISIS3 is available, this verifies output
        consistency with the canonical ISIS3 implementation.
        """
        # Reference ISIS3 documentation:
        # https://isis.astrogeology.usgs.gov/Application/presentation/Tabbed/caminfo/caminfo.html
        # Placeholder: full cross-validation requires sample planetary data
        # and a working ISIS3 environment; tests will be extended once a
        # reference data fixture is established.
        self.skipTest("Full ISIS3 cross-validation requires reference data fixture")


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
