"""
Testsuite for the self-contained Meeus ephemeris in p_lib.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.illumination.sunfraction/testsuite/test_ephemeris_meeus.py -v

These tests are a regression guard for the lunar sub-solar / sub-Earth
ephemeris (Meeus, Astronomical Algorithms ch.25/47/53). The reference values
are Meeus worked example 53.a (1992 April 12.0 TD, JD 2448724.5).

A history note: the sub-solar routine once returned the ANTI-solar point
(180 deg + sign flip); test_subsolar_is_not_antisolar locks that down.

If the adopted CSPICE library and the generic kernels are available, an extra
test cross-checks Meeus against SPICE subslr/subpnt.
"""

import os
import sys

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test

# Make the suite root importable (p_lib.py / p_spice.py live there).
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

import p_lib  # noqa: E402

JD_53A = 2448724.5  # 1992-04-12 00:00 TD

# Optional SPICE cross-check
_KDIR = "/tmp/spice_val"
_KERNELS = ["naif0012.tls", "pck00011.tpc", "de440s.bsp"]
HAS_KERNELS = all(os.path.isfile(os.path.join(_KDIR, k)) for k in _KERNELS)
try:
    import p_spice
    HAS_SPICE = p_spice.spice_available()
except Exception:
    HAS_SPICE = False


class TestMeeusEphemeris(TestCase):
    """Reference and physical-sanity checks for the Meeus ephemeris."""

    def test_subearth_libration_meeus_53a(self):
        """Sub-Earth libration matches Meeus 53.a (l=-1.21, b=+4.19)."""
        lat, lon = p_lib.subearth_point_moon(JD_53A)
        lon_signed = (lon + 180.0) % 360.0 - 180.0
        self.assertAlmostEqual(lat, 4.194, delta=0.05)
        self.assertAlmostEqual(lon_signed, -1.206, delta=0.05)

    def test_subsolar_is_not_antisolar(self):
        """Sub-solar latitude is POSITIVE at JD 53.a (~+1.45), not -1.45.

        Regression guard: feeding lambda_sun (instead of lambda_sun+180) to
        the libration formula yields the anti-solar point."""
        lat, lon = p_lib.subsolar_point_moon(JD_53A)
        self.assertGreater(lat, 0.0)
        self.assertAlmostEqual(lat, 1.45, delta=0.10)
        self.assertAlmostEqual(lon % 360.0, 68.0, delta=1.0)

    def test_subsolar_latitude_amplitude(self):
        """Over ~2 years the sub-solar latitude stays within the lunar
        obliquity-to-ecliptic envelope (|b| <= ~1.59 deg) and actually
        reaches near the extremes."""
        lats = [p_lib.subsolar_point_moon(2451545.0 + 2.0 * k)[0]
                for k in range(0, 366)]
        self.assertLessEqual(max(abs(min(lats)), abs(max(lats))), 1.65)
        self.assertGreater(max(lats), 1.40)
        self.assertLess(min(lats), -1.40)

    def test_subsolar_longitude_sweeps_full_circle(self):
        """Sub-solar selenographic longitude covers ~0..360 within a month."""
        lons = [p_lib.subsolar_point_moon(2451545.0 + 0.5 * k)[1]
                for k in range(0, 80)]
        self.assertLess(min(lons), 10.0)
        self.assertGreater(max(lons), 350.0)

    def test_subearth_libration_ranges(self):
        """Sub-Earth libration amplitudes match known optical+physical
        libration (~+-8 deg lon, ~+-7 deg lat) over 20 years."""
        lon_s = []
        lat_s = []
        for k in range(0, 366 * 20, 4):
            la, lo = p_lib.subearth_point_moon(2451545.0 + k)
            lat_s.append(la)
            lon_s.append((lo + 180.0) % 360.0 - 180.0)
        self.assertGreater(max(lon_s), 7.0)
        self.assertLess(min(lon_s), -7.0)
        self.assertGreater(max(lat_s), 6.0)
        self.assertLess(min(lat_s), -6.0)

    @test.skipIf(not (HAS_SPICE and HAS_KERNELS),
                 "libcspice and generic kernels required for SPICE cross-check")
    def test_meeus_matches_spice(self):
        """Meeus sub-solar/sub-Earth agree with SPICE (IAU_MOON) at 53.a."""
        for k in _KERNELS:
            p_spice.furnsh(os.path.join(_KDIR, k))
        et = p_spice.str2et("1992-04-12T00:00:00")
        s_lat, s_lon = p_spice.subsolar_point("MOON", "IAU_MOON", et,
                                              abcorr="NONE")
        e_lat, e_lon = p_spice.subobserver_point("MOON", "IAU_MOON", et,
                                                 "EARTH", abcorr="NONE")
        m_slat, m_slon = p_lib.subsolar_point_moon(JD_53A)
        m_elat, m_elon = p_lib.subearth_point_moon(JD_53A)
        self.assertAlmostEqual(m_slat, s_lat, delta=0.1)
        self.assertAlmostEqual(m_slon % 360.0, s_lon % 360.0, delta=0.5)
        self.assertAlmostEqual(m_elat, e_lat, delta=0.1)
        self.assertAlmostEqual((m_elon + 180) % 360 - 180,
                               (e_lon + 180) % 360 - 180, delta=0.5)


if __name__ == "__main__":
    test()
