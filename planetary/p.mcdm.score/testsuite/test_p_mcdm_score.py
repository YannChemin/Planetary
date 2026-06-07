"""
Testsuite for p.mcdm.score.

Run with:
    grass --tmp-location XY --exec python -m pytest \
        p.mcdm.score/testsuite/test_p_mcdm_score.py -v

Feeds synthetic criterion rasters and checks the WLC suitability output is a
normalized score in [0, 1].
"""

import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestMcdmScore(TestCase):

    prefix = "tsuit"
    rasters = ["t_slope", "t_rough", "t_illum", "t_evis", "t_orbit"]

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", n=900, s=0, e=900, w=0, res=30)
        # slope 0..~25 deg, roughness 0..~0.8 m, illum/evis/orbit 0..1
        gs.mapcalc("t_slope = abs(12.0 + 12.0*sin(row()*10))", overwrite=True)
        gs.mapcalc("t_rough = 0.4 + 0.4*sin(col()*10)", overwrite=True)
        gs.mapcalc("t_illum = 0.5 + 0.5*sin(row()*7)", overwrite=True)
        gs.mapcalc("t_evis  = 0.5 + 0.5*sin(col()*7)", overwrite=True)
        gs.mapcalc("t_orbit = 0.5 + 0.5*sin((row()+col())*9)", overwrite=True)

    @classmethod
    def tearDownClass(cls):
        outs = cls.rasters + [f"{cls.prefix}5_wlc", f"{cls.prefix}5_exclusion",
                              f"{cls.prefix}6_wlc", f"{cls.prefix}6_exclusion"]
        gs.run_command("g.remove", type="raster", name=",".join(outs),
                       flags="f", quiet=True)
        cls.del_temp_region()

    def test_wlc_score(self):
        pfx = f"{self.prefix}5"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_wlc,{pfx}_exclusion")
        self.assertModule("p.mcdm.score",
                          slope="t_slope", roughness="t_rough",
                          illumination="t_illum", earth_vis="t_evis",
                          weights="0.30,0.20,0.25,0.25,0.0",
                          method="wlc", prefix=pfx)
        self.assertRasterExists(f"{pfx}_wlc")
        self.assertRasterMinMax(f"{pfx}_wlc", 0, 1)

    def test_six_criteria_with_orbiter_vis(self):
        """Regression: the 6-element weights layout (slope, roughness,
        illumination, earth_vis, orbiter_vis, science) must be accepted and
        the orbiter_vis criterion must actually contribute to the WLC
        output. Backward-compat with the 5-element layout is verified by
        test_wlc_score above."""
        pfx = f"{self.prefix}6"
        gs.run_command("g.remove", type="raster", flags="f", quiet=True,
                       name=f"{pfx}_wlc,{pfx}_exclusion")
        self.assertModule("p.mcdm.score",
                          slope="t_slope", roughness="t_rough",
                          illumination="t_illum", earth_vis="t_evis",
                          orbiter_vis="t_orbit",
                          weights="0.20,0.15,0.20,0.15,0.30,0.0",
                          method="wlc", prefix=pfx)
        self.assertRasterExists(f"{pfx}_wlc")
        self.assertRasterMinMax(f"{pfx}_wlc", 0, 1)
        # With 30% weight on t_orbit and t_orbit varying over the region,
        # the result must have non-trivial variance — a regression where
        # orbiter_vis was silently dropped would produce a smoother map.
        stats = gs.parse_command("r.univar", map=f"{pfx}_wlc", flags="g")
        self.assertGreater(float(stats["stddev"]), 0.05,
            "WLC stddev too low — orbiter_vis criterion may be ignored.")


if __name__ == "__main__":
    test()
