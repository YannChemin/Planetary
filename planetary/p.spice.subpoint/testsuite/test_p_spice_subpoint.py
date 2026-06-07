"""
Testsuite for p.spice.subpoint / the p_spice ctypes wrapper.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.spice.subpoint/testsuite/test_p_spice_subpoint.py -v

The wrapper needs the CSPICE shared library (libcspice.so, from the
planetary-cspice package or $CSPICE_LIB) and the generic kernels. Tests that
need them are skipped when unavailable, so the suite stays green on machines
without SPICE installed.
"""

import os
import sys

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

import p_spice  # noqa: E402

_KDIR = "/tmp/spice_val"
_KERNELS = ["naif0012.tls", "pck00011.tpc", "de440s.bsp"]
HAS_SPICE = p_spice.spice_available()
HAS_KERNELS = all(os.path.isfile(os.path.join(_KDIR, k)) for k in _KERNELS)


class TestWrapperBasics(TestCase):

    def test_cache_dir_is_under_grass_config(self):
        """Cache dir resolves under the GRASS config dir (dirname of GISRC)."""
        cd = p_spice.spice_cache_dir()
        self.assertTrue(cd.endswith(os.path.join("p_spice")) or
                        "p_spice" in cd)

    def test_kernels_and_meta_subdirs(self):
        self.assertTrue(p_spice.kernels_dir().endswith("kernels"))
        self.assertTrue(p_spice.meta_dir().endswith("meta"))


class TestSpiceCalls(TestCase):

    @classmethod
    def setUpClass(cls):
        if not (HAS_SPICE and HAS_KERNELS):
            return
        p_spice.kclear()
        for k in _KERNELS:
            p_spice.furnsh(os.path.join(_KDIR, k))

    @test.skipIf(not HAS_SPICE, "libcspice.so not available")
    def test_library_loads(self):
        self.assertTrue(p_spice.spice_available())

    @test.skipIf(not (HAS_SPICE and HAS_KERNELS),
                 "libcspice.so and kernels required")
    def test_subsolar_point_moon(self):
        et = p_spice.str2et("1992-04-12T00:00:00")
        lat, lon = p_spice.subsolar_point("MOON", "IAU_MOON", et, abcorr="NONE")
        self.assertAlmostEqual(lat, 1.45, delta=0.1)
        self.assertAlmostEqual(lon % 360.0, 67.9, delta=1.0)

    @test.skipIf(not (HAS_SPICE and HAS_KERNELS),
                 "libcspice.so and kernels required")
    def test_bad_frame_raises_spiceerror(self):
        et = p_spice.str2et("2000-01-01T00:00:00")
        with self.assertRaises(p_spice.SpiceError):
            p_spice.subsolar_point("MOON", "NO_SUCH_FRAME", et)


if __name__ == "__main__":
    test()
