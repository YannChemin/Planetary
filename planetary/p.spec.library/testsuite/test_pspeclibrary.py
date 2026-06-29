"""Tests for p.spec.library — spectral library matching by SAM angle."""

import os
import math
import tempfile
import unittest

SPECTRA_DIR = os.path.join(os.path.dirname(__file__), "..", "spectra", "planetary")
MODULE_PY   = os.path.join(os.path.dirname(__file__), "..", "p.spec.library.py")


def _run_module(args):
    """Run p.spec.library in standalone (argparse) mode."""
    import importlib.util, sys
    spec = importlib.util.spec_from_file_location("p_spec_library", MODULE_PY)
    mod  = importlib.util.load_from_spec(spec)
    spec.loader.exec_module(mod)
    import io
    old_argv = sys.argv[:]
    old_stdout = sys.stdout
    sys.argv = ["p.spec.library"] + args
    sys.stdout = buf = io.StringIO()
    try:
        mod.main()
    except SystemExit:
        pass
    finally:
        sys.argv = old_argv
        sys.stdout = old_stdout
    return buf.getvalue()


class TestSpecLibraryBuiltin(unittest.TestCase):

    def _olivine_csv(self):
        return os.path.join(SPECTRA_DIR, "olivine_fo89.csv")

    def test_builtin_library_exists(self):
        """Built-in library directory contains at least 20 spectra."""
        self.assertTrue(os.path.isdir(SPECTRA_DIR))
        csvs = [f for f in os.listdir(SPECTRA_DIR) if f.endswith(".csv")]
        self.assertGreaterEqual(len(csvs), 20)

    def test_olivine_self_match(self):
        """Olivine_fo89 queried against itself scores SAM ≈ 0°."""
        import subprocess, sys
        result = subprocess.run(
            [sys.executable, MODULE_PY,
             "--spectrum", self._olivine_csv(),
             "--top", "1"],
            capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0)
        output = result.stdout
        lines = [l for l in output.splitlines() if not l.startswith("#") and "," in l]
        data = lines[1] if len(lines) > 1 else lines[0]  # skip CSV header
        cols = data.split(",")
        self.assertEqual(cols[1], "olivine_fo89")
        angle_deg = float(cols[4])
        self.assertAlmostEqual(angle_deg, 0.0, places=2)

    def test_olivine_top4_are_olivines(self):
        """Top 4 matches for olivine_fo89 are all olivines."""
        import subprocess, sys
        result = subprocess.run(
            [sys.executable, MODULE_PY,
             "--spectrum", self._olivine_csv(),
             "--top", "4"],
            capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0)
        lines = [l for l in result.stdout.splitlines()
                 if l and not l.startswith("#") and "," in l]
        data_rows = lines[1:]  # skip header
        self.assertEqual(len(data_rows), 4)
        for row in data_rows:
            name = row.split(",")[1]
            self.assertTrue(name.startswith("olivine_"), msg=f"Unexpected: {name}")

    def test_pyroxene_ranks_below_olivines(self):
        """Any pyroxene must rank below all olivines (larger SAM) for an olivine query."""
        import subprocess, sys
        result = subprocess.run(
            [sys.executable, MODULE_PY,
             "--spectrum", self._olivine_csv(),
             "--top", "28"],
            capture_output=True, text=True
        )
        lines = [l for l in result.stdout.splitlines()
                 if l and not l.startswith("#") and "," in l]
        data_rows = lines[1:]
        olivine_angles = [float(r.split(",")[3]) for r in data_rows
                          if r.split(",")[1].startswith("olivine_")]
        pyroxene_angles = [float(r.split(",")[3]) for r in data_rows
                           if "pyroxene" in r.split(",")[2] or r.split(",")[1].startswith("lcp_") or r.split(",")[1].startswith("hcp_")]
        if pyroxene_angles:
            self.assertGreater(min(pyroxene_angles), max(olivine_angles))

    def test_output_csv_written(self):
        """output= CSV file is written with correct columns."""
        import subprocess, sys
        with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as tmp:
            tmppath = tmp.name
        try:
            result = subprocess.run(
                [sys.executable, MODULE_PY,
                 "--spectrum", self._olivine_csv(),
                 "--top", "5",
                 "--output", tmppath],
                capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0)
            self.assertTrue(os.path.exists(tmppath))
            with open(tmppath) as f:
                lines = [l for l in f if not l.startswith("#")]
            header = lines[0].strip()
            self.assertIn("rank", header)
            self.assertIn("sam_angle_rad", header)
            data_rows = [l for l in lines[1:] if l.strip()]
            self.assertEqual(len(data_rows), 5)
        finally:
            os.unlink(tmppath)

    def test_max_angle_filter(self):
        """max_angle=0.01 keeps only near-perfect self-matches."""
        import subprocess, sys
        result = subprocess.run(
            [sys.executable, MODULE_PY,
             "--spectrum", self._olivine_csv(),
             "--top", "28",
             "--max_angle", "0.01"],
            capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0)
        lines = [l for l in result.stdout.splitlines()
                 if l and not l.startswith("#") and "," in l]
        data_rows = lines[1:]
        self.assertEqual(len(data_rows), 1)
        self.assertEqual(data_rows[0].split(",")[1], "olivine_fo89")


if __name__ == "__main__":
    unittest.main()
