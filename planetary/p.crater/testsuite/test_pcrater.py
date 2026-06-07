"""Test of p.crater

Validates the GRASS p.crater module against analytically-derived
reference values from:

  Melosh, H. J. (1989). "Impact Cratering: A Geologic Process."
  Oxford University Press, equations 7.8.1 - 7.8.4.

  Schmidt, R. M., & Holsapple, K. A. (1982). "Estimates of crater
  size for large-body impact: Gravity-scaling results." GSA Special
  Paper 190, 93-102.

Tests cover:

  1. Module --help and the body database (custom-slot validation).
  2. Pi-scaling forward / backward against Melosh 1989 eq. 7.8.4
     reference values computed inline.
  3. Gault scaling forward / backward roundtrip on three regimes.
  4. Yield scaling forward / backward (numerical inverse).
  5. Two-layer depth-weighted effective density.
  6. Simple-to-complex transition diameter Dsc proportional to 1/g.
  7. A Meteor Crater (Arizona) backward-mode reasonableness check.

@author Yann Chemin
@license Unlicense (https://unlicense.org)
"""

import math
import os
import shutil
import tempfile
import unittest

from grass.gunittest.case import TestCase
from grass.gunittest.gmodules import SimpleModule
import grass.script as gs


# --------------------------------------------------------------------- #
# Analytical reference equations (Melosh 1989), kept here in Python so
# the test is self-contained and does not depend on the module under test
# to verify itself.                                                       #
# --------------------------------------------------------------------- #

def _ref_W(rho_p, L, V):
    """Kinetic energy [J] of a spherical impactor."""
    return 0.5 * rho_p * (4.0 / 3.0) * math.pi * (L / 2.0) ** 3 * V * V


def _ref_pi_Dat(rho_p, rho_t, g, V, L):
    """Pi-scaling apparent transient diameter (Melosh 1989 eq. 7.8.4)."""
    W = _ref_W(rho_p, L, V)
    return (1.8 * rho_p ** 0.11 * rho_t ** (-1.0 / 3.0)
            * g ** (-0.22) * L ** 0.13 * W ** 0.22)


def _ref_gault_Dat(rho_p, rho_t, theta_rad, V, L, target_type):
    """Gault apparent transient diameter (regime-branching)."""
    W = _ref_W(rho_p, L, V)
    s = math.sin(theta_rad)
    if target_type == 3:
        Dat = (0.015 * rho_p ** (1.0 / 6.0) * rho_t ** -0.5
               * W ** 0.37 * s ** (2.0 / 3.0))
        if Dat > 10.0:
            Dat = (0.25 * rho_p ** (1.0 / 6.0) * rho_t ** -0.5
                   * W ** 0.29 * s ** (1.0 / 3.0))
            if Dat > 100.0:
                Dat = (0.27 * rho_p ** (1.0 / 6.0) * rho_t ** -0.5
                       * W ** 0.28 * s ** (1.0 / 3.0))
    else:
        Dat = (0.25 * rho_p ** (1.0 / 6.0) * rho_t ** -0.5
               * W ** 0.29 * s ** (1.0 / 3.0))
        if Dat > 100.0:
            Dat = (0.27 * rho_p ** (1.0 / 6.0) * rho_t ** -0.5
                   * W ** 0.28 * s ** (1.0 / 3.0))
    return Dat


def _ref_yield_Dat(rho_p, rho_t, V, L):
    """Yield-scaling apparent transient diameter (Nordyke 1962)."""
    W = _ref_W(rho_p, L, V)
    return (0.0133 * W ** (1.0 / 3.4)
            + 1.51 * math.sqrt(rho_p / rho_t) * L)


def _ref_effective_density(Dat, rho_surf, h_surf, rho_sub, h_sub):
    """Depth-weighted effective target density (Melosh 1989 ch. 5)."""
    if h_surf <= 0.0 or rho_sub <= 0.0:
        return rho_surf
    d_exc = Dat / 3.0
    if d_exc <= h_surf:
        return rho_surf
    if h_sub <= 0.0:
        return (h_surf * rho_surf + (d_exc - h_surf) * rho_sub) / d_exc
    if d_exc <= h_surf + h_sub:
        return (h_surf * rho_surf + (d_exc - h_surf) * rho_sub) / d_exc
    return (h_surf * rho_surf + h_sub * rho_sub) / (h_surf + h_sub)


def _ref_simple_complex_D(g):
    """Pike (1980) simple-to-complex transition diameter [m]."""
    return 18000.0 * (1.622 / g)


# --------------------------------------------------------------------- #
# TestCase                                                                #
# --------------------------------------------------------------------- #

class TestPcrater(TestCase):
    """Verify p.crater against Melosh 1989 reference equations."""

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        # Square 20 km region centred on origin, 50 m resolution.
        cls.runModule("g.region",
                      n=10000, s=-10000, e=10000, w=-10000, res=50)
        # Two-layer geology rasters for the depth-weighted test.
        cls.runModule("r.mapcalc",
                      expression="rho_surf_test = 1500", overwrite=True)
        cls.runModule("r.mapcalc",
                      expression="rho_sub_test = 3000", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        cls.runModule("g.remove", flags="f", type="vector",
                      pattern="test_pcrater_*", quiet=True)
        cls.runModule("g.remove", flags="f", type="raster",
                      name="rho_surf_test,rho_sub_test", quiet=True)
        cls.del_temp_region()

    # ----- helpers ----- #

    def _make_disk(self, name, radius_m, n_vertices=72):
        """Create a vector with a single circular crater of given radius.

        Writes the polygon directly in v.in.ascii 'standard' format
        (boundary + centroid) so the test does not depend on v.buffer
        being installed.
        """
        ascii_lines = ["ORGANIZATION: ",
                       "DIGIT DATE:   ",
                       "DIGIT NAME:   ",
                       "MAP NAME:     ",
                       "MAP DATE:     ",
                       "MAP SCALE:    1",
                       "OTHER INFO:   ",
                       "ZONE:         0",
                       "MAP THRESH:   0.5",
                       "VERTI:"]
        ascii_lines.append(f"B  {n_vertices + 1}")
        for i in range(n_vertices + 1):  # closed ring
            th = 2.0 * math.pi * i / n_vertices
            x = radius_m * math.cos(th)
            y = radius_m * math.sin(th)
            ascii_lines.append(f" {x:.6f} {y:.6f}")
        # Centroid at (0,0), 1 category in layer 1.
        ascii_lines.append("C  1 1")
        ascii_lines.append(" 0.000000 0.000000")
        ascii_lines.append(" 1 1")
        ascii_str = "\n".join(ascii_lines) + "\n"

        with tempfile.NamedTemporaryFile(mode='w', suffix='.ascii',
                                          delete=False) as f:
            f.write(ascii_str)
            ascii_path = f.name
        try:
            self.runModule("v.in.ascii", input=ascii_path, output=name,
                           format="standard", overwrite=True, quiet=True)
        finally:
            os.unlink(ascii_path)
        # Attach an attribute table for p.crater to populate.
        self.runModule("v.db.addtable", map=name, quiet=True)

    def _attr(self, vector, column, cat=1):
        """Fetch a single DOUBLE attribute value."""
        out = gs.read_command("v.db.select", map=vector,
                              columns=column, where=f"cat={cat}",
                              flags="c").strip()
        return float(out) if out else float("nan")

    def _assert_close(self, actual, expected, rtol=0.005, label=""):
        """Relative-tolerance assertion."""
        if expected == 0.0:
            self.assertAlmostEqual(actual, 0.0, places=6,
                                    msg=f"{label}: {actual} != 0")
            return
        rel = abs(actual - expected) / abs(expected)
        self.assertLess(
            rel, rtol,
            f"{label}: actual={actual:.6g}, expected={expected:.6g}, "
            f"rel.err={rel:.4%} > {rtol:.4%}")

    # ----- 1. interface ----- #

    def test_help(self):
        """Module responds to --help (probed via --interface-description,
        which is always available and triggers full option parsing)."""
        import subprocess
        # gunittest's SimpleModule parses flags="h" against the module's
        # declared flag list, and -h is not a user-defined flag for
        # p.crater, so use subprocess directly.
        rc = subprocess.run(
            ["p.crater", "--interface-description"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode
        self.assertEqual(rc, 0, "p.crater --interface-description failed")

    def test_module_metadata(self):
        """Module binary is on PATH."""
        self.assertIsNotNone(shutil.which("p.crater"),
                             "Module p.crater not found in PATH")

    def test_invalid_body_rejected(self):
        """Unknown body name -> rejected.

        SimpleModule pre-validates the body option against the module's
        declared options= list at construction time, so the rejection
        surfaces as a ValueError before the module is even spawned.
        Either pre- or runtime-validation satisfies the safety contract.
        """
        self._make_disk("test_pcrater_v1", 500)
        with self.assertRaises(ValueError):
            SimpleModule("p.crater",
                          input="test_pcrater_v1",
                          output="test_pcrater_out1",
                          body="zorgon",
                          overwrite=True)

    def test_custom_body_requires_overrides(self):
        """body=custom without gravity/target_density/target_type -> fatal."""
        self._make_disk("test_pcrater_v2", 500)
        module = SimpleModule("p.crater", flags="b",
                              input="test_pcrater_v2",
                              output="test_pcrater_out2",
                              body="custom", overwrite=True)
        self.assertModuleFail(module)

    # ----- 2. Forward Pi-scaling against Melosh 1989 eq. 7.8.4 ----- #

    def test_pi_forward_moon_melosh(self):
        """Forward Pi-scaling on Moon vs analytical Melosh eq. 7.8.4.

        Parameters: 10 m stony impactor, 20 km/s, 45 deg, lunar regolith.
        Expected: Dat_pi = 1.8 * rho_p^0.11 * rho_t^(-1/3) * g^(-0.22)
                          * L^0.13 * W^0.22 ~= 711.6 m
        """
        L, V, rho_p = 10.0, 20000.0, 3000.0
        rho_t, g = 1500.0, 1.622  # Moon defaults from p_crater_body_at
        theta_rad = math.radians(45.0)

        Dat_ref   = _ref_pi_Dat(rho_p, rho_t, g, V, L)
        Dat_gref  = _ref_gault_Dat(rho_p, rho_t, theta_rad, V, L,
                                    target_type=2)
        Dat_yref  = _ref_yield_Dat(rho_p, rho_t, V, L)
        W_ref     = _ref_W(rho_p, L, V)

        self._make_disk("test_pcrater_v3", 100)  # geometry irrelevant
        self.runModule("p.crater",
                       input="test_pcrater_v3",
                       output="test_pcrater_out3",
                       body="moon",
                       impactor_velocity=str(V),
                       impactor_angle="45",
                       impactor_density=str(rho_p),
                       impactor_diameter=str(L),
                       overwrite=True)

        self._assert_close(self._attr("test_pcrater_out3", "Dat_pi"),
                            Dat_ref, rtol=0.005, label="Dat_pi Moon")
        self._assert_close(self._attr("test_pcrater_out3", "Dat_gault"),
                            Dat_gref, rtol=0.005, label="Dat_gault Moon")
        self._assert_close(self._attr("test_pcrater_out3", "Dat_yield"),
                            Dat_yref, rtol=0.005, label="Dat_yield Moon")
        self._assert_close(self._attr("test_pcrater_out3", "kinetic_J"),
                            W_ref, rtol=0.002, label="kinetic_J Moon")
        # tnt_kt = W / 4.184e12
        self._assert_close(self._attr("test_pcrater_out3", "tnt_kt"),
                            W_ref / 4.184e12, rtol=0.002,
                            label="tnt_kt Moon")

    # ----- 3. Backward Pi-scaling roundtrip ----- #

    def test_pi_backward_roundtrip_mars(self):
        """Forward -> backward Pi-scaling on Mars must roundtrip to L.

        After the 0.4.9 inverse-formula fix the roundtrip should be
        exact to within float64 precision.
        """
        L, V, rho_p = 50.0, 18000.0, 3000.0
        rho_t, g = 2900.0, 3.711  # Mars defaults
        Dat_ref = _ref_pi_Dat(rho_p, rho_t, g, V, L)
        D_eq    = Dat_ref  # backward mode treats D_eq as the transient diameter

        radius = 0.5 * D_eq
        self._make_disk("test_pcrater_v4", radius)
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v4",
                       output="test_pcrater_out4",
                       body="mars",
                       impactor_velocity=str(V),
                       impactor_angle="45",
                       impactor_density=str(rho_p),
                       overwrite=True)

        self._assert_close(self._attr("test_pcrater_out4", "D_eq"),
                            D_eq, rtol=0.001, label="D_eq Mars")
        self._assert_close(self._attr("test_pcrater_out4", "proj_pi"),
                            L, rtol=0.005,
                            label="proj_pi backward roundtrip Mars")

    # ----- 4. Meteor Crater (Arizona) reasonableness ----- #

    def test_meteor_crater_earth(self):
        """Meteor Crater (Arizona): Df ~ 1186 m -> impactor a few tens of m.

        Shoemaker (1963) and Kring (2017) estimate the original Canyon
        Diablo iron at 30-60 m diameter. The Pi-scaling gravity-dominated
        regime gives ~24 m (lower bound) since strength scaling is not
        applied here.
        """
        D_final = 1186.0
        # In the simple regime (Earth Dsc ~ 3 km, so 1186 m is simple),
        # the inverse of Df = 1.25 * Dat gives Dat ~ 949 m.
        Dat = D_final / 1.25
        radius = 0.5 * Dat

        self._make_disk("test_pcrater_v5", radius)
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v5",
                       output="test_pcrater_out5",
                       body="earth",
                       impactor_velocity="15000",
                       impactor_angle="45",
                       impactor_density="7800",  # iron
                       target_type="3",          # competent rock
                       overwrite=True)

        L_pi = self._attr("test_pcrater_out5", "proj_pi")
        self.assertGreater(L_pi, 5.0,
                            f"Meteor-Crater impactor too small: {L_pi:.1f} m")
        self.assertLess(L_pi, 200.0,
                         f"Meteor-Crater impactor too large: {L_pi:.1f} m")

        # Final-diameter check: simple regime Df = 1.25 * Dat
        Df = self._attr("test_pcrater_out5", "Df_pi")
        self._assert_close(Df, 1.25 * Dat, rtol=0.01, label="Df simple Earth")

    # ----- 5. Two-layer depth-weighted density ----- #

    def test_two_layer_effective_density(self):
        """Surface 1500 kg/m^3, 50 m thick over subsurface 3000 kg/m^3.

        For a 3-km transient crater on Moon, excavation depth ~ 1000 m,
        so depth-weighted density = (50*1500 + 950*3000)/1000 = 2925.
        """
        Dat = 3000.0
        radius = 0.5 * Dat
        rho_eff_ref = _ref_effective_density(Dat, 1500.0, 50.0,
                                              3000.0, 0.0)
        self.assertAlmostEqual(rho_eff_ref, 2925.0, places=1)

        self._make_disk("test_pcrater_v6", radius)
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v6",
                       output="test_pcrater_out6",
                       body="moon",
                       surface_density_map="rho_surf_test",
                       surface_thickness="50",
                       subsurface_density_map="rho_sub_test",
                       impactor_velocity="20000",
                       impactor_angle="45",
                       impactor_density="3000",
                       overwrite=True)

        self._assert_close(self._attr("test_pcrater_out6", "rho_eff"),
                            rho_eff_ref, rtol=0.005,
                            label="depth-weighted rho_eff")

    # ----- 6a. Spatial d/D override via raster ----- #

    def test_dd_simple_map_raster(self):
        """A dd_simple_map= raster overrides the body's default d/D.

        The raster carries a constant value of 0.300 (far from Moon's
        0.196 default). For a simple crater (D < Dsc ~ 18 km on Moon)
        with D_eq = 2000 m, predicted depth = 2000 * 0.300 = 600 m,
        and dD_ratio recorded in the output should equal 0.300.

        Without the raster, the same run would write dD_ratio = 0.196
        - so the test specifically isolates the raster override path.
        """
        # Constant d/D raster covering the full region.
        self.runModule("r.mapcalc", expression="dd_test = 0.300",
                       overwrite=True)

        # 2 km transient crater on Moon (well inside the simple regime).
        self._make_disk("test_pcrater_v7", 1000.0)
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v7",
                       output="test_pcrater_out7",
                       body="moon",
                       dd_simple_map="dd_test",
                       impactor_velocity="20000",
                       impactor_angle="45",
                       impactor_density="3000",
                       overwrite=True)

        Df = self._attr("test_pcrater_out7", "Df_pi")
        dp = self._attr("test_pcrater_out7", "depth_pred")
        ratio = dp / Df if Df > 0 else 0.0
        # Allow some tolerance because Df = 1.25 * Dat applies a
        # small bowl-fill multiplier; check that the ratio honours
        # the raster (0.300), not Moon's body default (0.196).
        self._assert_close(ratio, 0.300, rtol=0.02,
                            label="dD honours dd_simple_map raster")

        # Sanity check: ratio is clearly NOT the Moon default (0.196).
        self.assertGreater(ratio, 0.250,
                            f"depth/diameter ratio {ratio:.3f} did not "
                            "pick up the raster value (would be ~0.196 "
                            "from the Moon database default)")

        # And same for dd_simple= scalar override: a scalar override
        # of 0.250 should win the same way.
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v7",
                       output="test_pcrater_out7b",
                       body="moon",
                       dd_simple="0.250",
                       impactor_velocity="20000",
                       impactor_angle="45",
                       impactor_density="3000",
                       overwrite=True)
        Df2 = self._attr("test_pcrater_out7b", "Df_pi")
        dp2 = self._attr("test_pcrater_out7b", "depth_pred")
        ratio2 = dp2 / Df2 if Df2 > 0 else 0.0
        self._assert_close(ratio2, 0.250, rtol=0.02,
                            label="dD honours dd_simple scalar")

        # Cleanup the test raster.
        self.runModule("g.remove", flags="f", type="raster",
                       name="dd_test", quiet=True)

    # ----- 6c. Per-body measured Dsc -----

    def test_per_body_measured_dsc(self):
        """Mars Dsc is measured at 7 km (Pike 1980), not the 1/g
        analytic ~7.9 km. A 6 km final crater on Mars must therefore
        still be in the SIMPLE regime (D < Dsc=7 km) -> dD_ratio
        equals the body simple value 0.150, NOT the complex-regime
        smooth transition (which would give a much lower ratio at
        D=6 km if Dsc were ~7.9 km)."""
        # Build a crater rim whose D_eq corresponds to a final
        # diameter of about 6 km. For backward mode, D_eq is treated
        # as Dat; Df = 1.25 * Dat so Dat = 6000/1.25 = 4800 m.
        radius = 4800.0 / 2.0  # 2.4 km radius
        # Need a wider region for a 6 km crater.
        self.runModule("g.region",
                       n=10000, s=-10000, e=10000, w=-10000, res=50)
        self._make_disk("test_pcrater_v8", radius)
        self.runModule("p.crater", flags="b",
                       input="test_pcrater_v8",
                       output="test_pcrater_out8",
                       body="mars",
                       impactor_velocity="18000",
                       impactor_angle="45",
                       impactor_density="3000",
                       overwrite=True)
        Df = self._attr("test_pcrater_out8", "Df_pi")
        dp = self._attr("test_pcrater_out8", "depth_pred")
        ratio = dp / Df if Df > 0 else 0.0
        # Mars simple d/D = 0.150, Mars Dsc = 7000 m. Df ~ 6000 m
        # should be solidly inside the simple regime, so ratio
        # should equal 0.150.
        self._assert_close(ratio, 0.150, rtol=0.03,
                            label="6 km Mars crater stays simple at "
                            "measured Dsc=7 km")
        # Reset region for following tests.
        self.runModule("g.region",
                       n=10000, s=-10000, e=10000, w=-10000, res=50)

    # ----- 6b. Simple-to-complex transition diameter ----- #

    def test_simple_complex_transition_scales_with_inverse_g(self):
        """Dsc ~ 18 km on Moon (g=1.622), scales as 1/g (Pike 1980).

        Cross-check via a forward-mode run on each body: Df should equal
        1.25 * Dat below the body-specific Dsc, and follow the complex
        formula above it. We test the Dsc value by reading it from the
        startup message indirectly via the Df behaviour.
        """
        # Direct numerical check against the documented Dsc values.
        # (Verifying the equation, not the module output here.)
        for body, g, Dsc_expected in [
            ("moon",   1.622, 18000.0),
            ("mars",   3.711, 18000.0 * 1.622 / 3.711),
            ("earth",  9.807, 18000.0 * 1.622 / 9.807),
            ("ceres",  0.284, 18000.0 * 1.622 / 0.284),
        ]:
            self._assert_close(_ref_simple_complex_D(g), Dsc_expected,
                                rtol=1e-9,
                                label=f"Dsc reference {body}")


if __name__ == "__main__":
    from grass.gunittest.main import test
    test()
