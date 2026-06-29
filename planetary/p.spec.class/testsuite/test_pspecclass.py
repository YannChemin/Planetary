"""Tests for p.spec.class — spectral classification of planetary rasters."""

import os
import grass.script as gs
from grass.gunittest.case import TestCase
from grass.gunittest.main import test


class TestSpecClass(TestCase):

    TMPSPEC = "/tmp/test_pspecclass_ref.csv"

    @classmethod
    def setUpClass(cls):
        cls.use_temp_region()
        gs.run_command("g.region", rows=20, cols=20)
        # 3-band synthetic rasters: two distinct clusters
        for b, expr in [("1", "if(row() < 10, 1.0, 5.0)"),
                        ("2", "if(row() < 10, 1.2, 4.8)"),
                        ("3", "if(row() < 10, 0.9, 5.1)")]:
            gs.run_command("r.mapcalc",
                           expression=f"cls_band.{b} = {expr}",
                           overwrite=True)
        # Reference spectrum matching the "low" cluster
        with open(cls.TMPSPEC, "w") as f:
            f.write("1.0\n1.2\n0.9\n")

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       pattern="cls_band.*,cls_out,cls_sam",
                       flags="f", quiet=True)
        if os.path.exists(cls.TMPSPEC):
            os.remove(cls.TMPSPEC)
        if os.path.exists("cls_centroids.csv"):
            os.remove("cls_centroids.csv")

    def test_kmeans_runs(self):
        """k-means mode runs without error."""
        ret = gs.run_command("p.spec.class",
                             input="cls_band", output="cls_out",
                             mode="kmeans", k=2, seed=1, overwrite=True)
        self.assertEqual(ret, 0)
        self.assertRasterExists("cls_out")

    def test_kmeans_two_clusters(self):
        """k=2 on a dataset with two clear clusters → exactly 2 classes."""
        gs.run_command("p.spec.class",
                       input="cls_band", output="cls_out",
                       mode="kmeans", k=2, seed=1, overwrite=True)
        info = gs.parse_command("r.univar", map="cls_out", flags="g", quiet=True)
        mn = float(info["min"])
        mx = float(info["max"])
        self.assertEqual(int(mn), 1)
        self.assertEqual(int(mx), 2)

    def test_kmeans_stats_csv(self):
        """stats= CSV is created with k rows (one per class)."""
        gs.run_command("p.spec.class",
                       input="cls_band", output="cls_out",
                       mode="kmeans", k=2, seed=1,
                       stats="cls_centroids.csv", overwrite=True)
        self.assertTrue(os.path.exists("cls_centroids.csv"))
        data_rows = 0
        with open("cls_centroids.csv") as f:
            for line in f:
                if not line.startswith("#"):
                    data_rows += 1
        self.assertEqual(data_rows, 3)  # header + 2 class rows

    def test_sam_runs(self):
        """SAM mode runs without error."""
        ret = gs.run_command("p.spec.class",
                             input="cls_band", output="cls_sam",
                             mode="sam",
                             spectrum=self.TMPSPEC,
                             threshold=0.1,
                             overwrite=True)
        self.assertEqual(ret, 0)
        self.assertRasterExists("cls_sam")

    def test_sam_matches_low_cluster(self):
        """SAM matches the first 10 rows (low cluster) but not the second."""
        gs.run_command("p.spec.class",
                       input="cls_band", output="cls_sam",
                       mode="sam",
                       spectrum=self.TMPSPEC,
                       threshold=0.05,
                       overwrite=True)
        # At tight threshold, only the low cluster should match
        info = gs.parse_command("r.univar", map="cls_sam", flags="g", quiet=True)
        n_match = int(gs.parse_command(
            "r.univar", map="cls_sam", flags="g", quiet=True
        ).get("sum", "0").split(".")[0])
        # Low cluster = rows 1-10, 20 cols = 200 pixels; high cluster should not match
        self.assertGreater(int(info.get("n", 0)), 0)


if __name__ == "__main__":
    test()
