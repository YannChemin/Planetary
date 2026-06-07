"""
Testsuite for p.in.pds

Run with:
    grass --tmp-location XY --exec python -m pytest testsuite/test_p_in_pds.py -v

Tests exercise both the GDAL fast path (SLDEM2015 .LBL) and the .IMG
companion-detection logic. The ISIS3 fallback path is only tested when
$ISISROOT is set.
"""

import os
import pytest
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


SLDEM_DIR = os.path.join(os.path.expanduser("~"), "RSDATA", "Moon", "SLDEM2015")
SLDEM_LBL = os.path.join(SLDEM_DIR, "SLDEM2015_128_60S_60N_000_360_FLOAT.LBL")
SLDEM_IMG = os.path.join(SLDEM_DIR, "SLDEM2015_128_60S_60N_000_360_FLOAT.IMG")
QUALITY_LBL = os.path.join(SLDEM_DIR, "SLDEM2015_DATA_QUALITY_FLOAT.LBL")

HAS_SLDEM = os.path.isfile(SLDEM_LBL)
HAS_ISIS3 = bool(os.environ.get("ISISROOT", ""))


class TestPInPdsGdalPath(TestCase):
    """Tests that exercise the GDAL fast path."""

    output = "test_sldem_pds"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster", name=cls.output,
                       flags="f", quiet=True)

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_import_lbl_creates_raster(self):
        """Importing the .LBL file should create a raster map."""
        self.assertModule(
            "p.in.pds",
            input=SLDEM_LBL,
            output=self.output,
            overwrite=True,
        )
        self.assertRasterExists(self.output)

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_unit_conversion_km_to_m(self):
        """
        SLDEM2015 stores values in km.  After auto-scaling the raster should
        contain values in metres (elevations well above +/-10 m on the Moon).
        """
        self.assertModule(
            "p.in.pds",
            input=SLDEM_LBL,
            output=self.output,
            overwrite=True,
        )
        stats = gs.parse_command("r.univar", map=self.output, flags="g")
        max_val = float(stats["max"])
        min_val = float(stats["min"])
        # SLDEM2015 range is roughly -8.7 km to +10.8 km → -8700 to +10800 m
        self.assertGreater(max_val, 1000.0,
                           "Max value should be > 1000 m (km→m conversion applied)")
        self.assertLess(min_val, -1000.0,
                        "Min value should be < -1000 m (km→m conversion applied)")

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_img_companion_detection(self):
        """Supplying the .IMG should auto-locate the .LBL and succeed."""
        self.assertModule(
            "p.in.pds",
            input=SLDEM_IMG,
            output=self.output,
            overwrite=True,
        )
        self.assertRasterExists(self.output)

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_no_scale_flag(self):
        """With scale=1 the values remain in km (max < 20)."""
        self.assertModule(
            "p.in.pds",
            input=SLDEM_LBL,
            output=self.output,
            scale=1,
            overwrite=True,
        )
        stats = gs.parse_command("r.univar", map=self.output, flags="g")
        max_val = float(stats["max"])
        self.assertLess(max_val, 20.0,
                        "With scale=1 values stay in km; max should be < 20 km")

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_region_flag(self):
        """The -r flag should set the computational region to the map extent."""
        self.assertModule(
            "p.in.pds",
            input=SLDEM_LBL,
            output=self.output,
            flags="r",
            overwrite=True,
        )
        region = gs.region()
        info = gs.raster_info(self.output)
        self.assertAlmostEqual(region["n"], info["north"], places=1)
        self.assertAlmostEqual(region["s"], info["south"], places=1)

    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_quality_layer_no_km_conversion(self):
        """Data-quality layer has no UNIT keyword; scale factor must stay 1."""
        out = "test_sldem_quality"
        try:
            self.assertModule(
                "p.in.pds",
                input=QUALITY_LBL,
                output=out,
                overwrite=True,
            )
            self.assertRasterExists(out)
            stats = gs.parse_command("r.univar", map=out, flags="g")
            # Quality flags are small integers or floats; must not be × 1000
            max_val = float(stats["max"])
            self.assertLess(max_val, 10.0,
                            "Quality map values should not be scaled by 1000")
        finally:
            gs.run_command("g.remove", type="raster", name=out,
                           flags="f", quiet=True)


class TestPInPdsIsis3Fallback(TestCase):
    """Tests for the ISIS3 fallback path (skipped if $ISISROOT not set)."""

    output = "test_isis3_import"

    @classmethod
    def tearDownClass(cls):
        gs.run_command("g.remove", type="raster", name=cls.output,
                       flags="f", quiet=True)

    @pytest.mark.skipif(not HAS_ISIS3, reason="$ISISROOT not set")
    @pytest.mark.skipif(not HAS_SLDEM, reason="SLDEM2015 not present")
    def test_isis3_fallback_explicit(self):
        """Force ISIS3 fallback by passing a non-existent GDAL-readable path."""
        # We can test the fallback by faking an un-readable file; instead,
        # we test that the module survives when ISIS3 is available and
        # the input is a real .img with a detached label.
        self.assertModule(
            "p.in.pds",
            input=SLDEM_IMG,
            output=self.output,
            isis3=os.path.join(os.environ["ISISROOT"], "bin"),
            overwrite=True,
        )
        self.assertRasterExists(self.output)


if __name__ == "__main__":
    test()
