#!/usr/bin/env python3
"""
p_landing_qt.py — Planetary Landing Site Evaluation Wizard
Qt6-native standalone wizard that drives the full p.* pipeline.

Usage:  python3 p_landing_qt.py [--gisdb PATH] [--session FILE]
Requirements: PyQt6, GRASS GIS 8.x in $PATH

License: Unlicense (https://unlicense.org)
Author:  Yann Chemin
"""

import sys
import os
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PyQt6.QtCore import (Qt, QThread, pyqtSignal, QObject, QTimer,
                           QSize, QSettings)
from PyQt6.QtGui  import (QFont, QIcon, QPixmap, QColor, QPalette,
                           QTextCursor)
from PyQt6.QtWidgets import (
    QApplication, QWizard, QWizardPage,
    QVBoxLayout, QHBoxLayout, QFormLayout, QGridLayout,
    QLabel, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox,
    QCheckBox, QPushButton, QTextEdit, QProgressBar,
    QGroupBox, QFileDialog, QTableWidget, QTableWidgetItem,
    QHeaderView, QSplitter, QWidget, QSizePolicy,
    QMessageBox, QTabWidget, QDialog, QDialogButtonBox,
)

# ── Detect GRASS environment ─────────────────────────────────────────────────

def _find_grass():
    """Return (gisbase, python_path) or raise RuntimeError."""
    gisbase = os.environ.get("GISBASE", "")
    if not gisbase:
        for candidate in ["/usr/local/grass86", "/usr/lib/grass86",
                          "/usr/local/grass", "/usr/lib/grass"]:
            if os.path.isdir(candidate):
                gisbase = candidate
                break
    if not gisbase:
        result = subprocess.run(["grass", "--config", "path"],
                                capture_output=True, text=True)
        gisbase = result.stdout.strip()
    if not gisbase or not os.path.isdir(gisbase):
        raise RuntimeError("GRASS GIS not found. Set GISBASE or install GRASS.")
    pypath = os.path.join(gisbase, "etc", "python")
    return gisbase, pypath


try:
    GISBASE, GRASS_PYPATH = _find_grass()
    if GRASS_PYPATH not in sys.path:
        sys.path.insert(0, GRASS_PYPATH)
    os.environ["GISBASE"] = GISBASE
    import grass.script as gs
    GRASS_OK = True
except Exception as _e:
    GRASS_OK = False
    _GRASS_ERR = str(_e)

SRCDIR = Path(__file__).parent.parent   # p.* modules are siblings

# ── GRASS module runner thread ───────────────────────────────────────────────

class GrassRunner(QThread):
    """Run a GRASS command in a background thread; emit log lines and result."""
    log_line  = pyqtSignal(str)
    finished  = pyqtSignal(bool, str)   # (success, message)
    progress  = pyqtSignal(int)         # 0-100

    def __init__(self, cmd, env=None, parent=None):
        super().__init__(parent)
        self.cmd = cmd          # list of strings
        self.env = env or os.environ.copy()

    def run(self):
        try:
            proc = subprocess.Popen(
                self.cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                env=self.env,
            )
            pct = 0
            for line in proc.stdout:
                line = line.rstrip()
                self.log_line.emit(line)
                # Extract GRASS percent progress
                if "%" in line:
                    try:
                        tok = [t for t in line.split() if t.endswith("%")]
                        if tok:
                            pct = min(99, int(tok[-1].rstrip("%")))
                            self.progress.emit(pct)
                    except ValueError:
                        pass
            proc.wait()
            self.progress.emit(100)
            ok = proc.returncode == 0
            self.finished.emit(ok, "" if ok else f"Exit code {proc.returncode}")
        except Exception as exc:
            self.finished.emit(False, str(exc))


class PythonScriptRunner(GrassRunner):
    """Run a p.* Python script via GRASS's python3 within a mapset."""

    def __init__(self, script, args, gisdb, location, mapset="PERMANENT",
                 parent=None):
        env = os.environ.copy()
        env["GISBASE"] = GISBASE
        env["GISDBASE"] = gisdb
        env["LOCATION_NAME"] = location
        env["MAPSET"] = mapset
        gisrc = self._make_gisrc(gisdb, location, mapset)
        env["GISRC"] = gisrc
        lib = os.path.join(GISBASE, "lib")
        env["LD_LIBRARY_PATH"] = lib + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        env["PATH"] = os.path.join(GISBASE, "bin") + os.pathsep + \
                      os.path.join(GISBASE, "scripts") + os.pathsep + \
                      env.get("PATH", "")
        full_script = str(SRCDIR / script)
        cmd = ["python3", full_script] + args
        super().__init__(cmd, env=env, parent=parent)
        self._gisrc = gisrc

    @staticmethod
    def _make_gisrc(gisdb, location, mapset):
        fd, path = tempfile.mkstemp(suffix=".gisrc", prefix="planding_")
        os.close(fd)
        with open(path, "w") as f:
            f.write(f"GISDBASE: {gisdb}\n")
            f.write(f"LOCATION_NAME: {location}\n")
            f.write(f"MAPSET: {mapset}\n")
        return path

    def run(self):
        super().run()
        try:
            os.unlink(self._gisrc)
        except OSError:
            pass


# ── Shared log widget mixin ──────────────────────────────────────────────────

class LogMixin:
    """Provide a QTextEdit log + QProgressBar + run/stop button."""

    def _build_log_panel(self):
        grp = QGroupBox("Output log")
        lay = QVBoxLayout(grp)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Monospace", 9))
        self._log.setMinimumHeight(140)
        lay.addWidget(self._log)
        self._pbar = QProgressBar()
        self._pbar.setRange(0, 100)
        self._pbar.setValue(0)
        lay.addWidget(self._pbar)
        return grp

    def _append_log(self, text):
        self._log.append(text)
        self._log.moveCursor(QTextCursor.MoveOperation.End)

    def _set_progress(self, pct):
        self._pbar.setValue(pct)

    def _clear_log(self):
        self._log.clear()
        self._pbar.setValue(0)


# ── Session state ────────────────────────────────────────────────────────────

class Session:
    """Persists wizard parameter state to a JSON file."""

    def __init__(self, path=None):
        self.path = path or Path.home() / ".p_landing_wizard_session.json"
        self.data = {}
        self._load()

    def _load(self):
        try:
            with open(self.path) as f:
                self.data = json.load(f)
        except (OSError, json.JSONDecodeError):
            self.data = {}

    def save(self):
        with open(self.path, "w") as f:
            json.dump(self.data, f, indent=2)

    def get(self, key, default=None):
        return self.data.get(key, default)

    def set(self, key, value):
        self.data[key] = value
        self.save()


# ═══════════════════════════════════════════════════════════════════════════
# Wizard pages
# ═══════════════════════════════════════════════════════════════════════════

class SetupPage(QWizardPage, LogMixin):
    """Page 1 — GRASS database, location, body and mission config."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 1 — Setup")
        self.setSubTitle("Select the GRASS database, location, body descriptor "
                         "and mission configuration.")
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)

        # GRASS database
        grp = QGroupBox("GRASS GIS environment")
        form = QFormLayout(grp)

        self._gisdb = QLineEdit(self.session.get(
            "gisdb", str(Path.home() / "grassdata")))
        btn_db = QPushButton("Browse…")
        btn_db.clicked.connect(self._pick_gisdb)
        row = QHBoxLayout(); row.addWidget(self._gisdb); row.addWidget(btn_db)
        form.addRow("GRASS database:", row)

        self._location = QLineEdit(self.session.get("location", "Moon_SouthPole_5m"))
        form.addRow("Location:", self._location)

        self._mapset = QLineEdit(self.session.get("mapset", "PERMANENT"))
        form.addRow("Mapset:", self._mapset)
        lay.addWidget(grp)

        # Body + mission
        grp2 = QGroupBox("Body & mission")
        form2 = QFormLayout(grp2)

        srcdir = str(SRCDIR)
        self._body = QLineEdit(self.session.get(
            "body", os.path.join(srcdir, "bodies", "moon.json")))
        btn_body = QPushButton("Browse…")
        btn_body.clicked.connect(lambda: self._pick_json(self._body))
        r2 = QHBoxLayout(); r2.addWidget(self._body); r2.addWidget(btn_body)
        form2.addRow("Body descriptor:", r2)

        self._mission = QLineEdit(self.session.get(
            "mission", os.path.join(srcdir, "missions", "luna27.json")))
        btn_miss = QPushButton("Browse…")
        btn_miss.clicked.connect(lambda: self._pick_json(self._mission))
        r3 = QHBoxLayout(); r3.addWidget(self._mission); r3.addWidget(btn_miss)
        form2.addRow("Mission config:", r3)
        lay.addWidget(grp2)

        lay.addStretch()

        # Register fields so QWizard can access them
        self.registerField("gisdb*", self._gisdb)
        self.registerField("location*", self._location)
        self.registerField("mapset", self._mapset)
        self.registerField("body*", self._body)
        self.registerField("mission*", self._mission)

    def _pick_gisdb(self):
        d = QFileDialog.getExistingDirectory(self, "Select GRASS database",
                                              self._gisdb.text())
        if d:
            self._gisdb.setText(d)

    def _pick_json(self, widget):
        p, _ = QFileDialog.getOpenFileName(self, "Select JSON", "",
                                            "JSON files (*.json)")
        if p:
            widget.setText(p)

    def validatePage(self):
        self.session.set("gisdb",    self._gisdb.text())
        self.session.set("location", self._location.text())
        self.session.set("mapset",   self._mapset.text())
        self.session.set("body",     self._body.text())
        self.session.set("mission",  self._mission.text())
        return True


class ImportPage(QWizardPage, LogMixin):
    """Page 2 — DEM import (p.in.pds / p.in.dem)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 2 — DEM Import")
        self.setSubTitle("Import the planetary DEM into the GRASS mapset.")
        self._runner = None
        self._done   = False
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)

        grp = QGroupBox("DEM file")
        form = QFormLayout(grp)
        self._dem_file = QLineEdit(self.session.get("dem_file", ""))
        btn = QPushButton("Browse…")
        btn.clicked.connect(self._pick_dem)
        row = QHBoxLayout(); row.addWidget(self._dem_file); row.addWidget(btn)
        form.addRow("PDS3 label / GeoTIFF:", row)

        self._dem_name = QLineEdit(self.session.get("dem_name", "lola_dem"))
        form.addRow("Output map name:", self._dem_name)
        lay.addWidget(grp)

        self._run_btn = QPushButton("▶  Import DEM")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)

        lay.addWidget(self._build_log_panel())
        self.registerField("dem_name", self._dem_name)

    def _pick_dem(self):
        p, _ = QFileDialog.getOpenFileName(
            self, "Select DEM file", "",
            "PDS label / GeoTIFF (*.lbl *.LBL *.tif *.tiff *.img *.cub)")
        if p:
            self._dem_file.setText(p)

    def _run(self):
        self._clear_log()
        self._done = False
        self._run_btn.setEnabled(False)
        inp = self._dem_file.text().strip()
        out = self._dem_name.text().strip()
        if not inp or not out:
            self._append_log("ERROR: Set DEM file and output name first.")
            self._run_btn.setEnabled(True)
            return

        self.session.set("dem_file", inp)
        self.session.set("dem_name", out)

        args = [f"input={inp}", f"output={out}", "--overwrite"]
        self._runner = PythonScriptRunner(
            "p.in.pds/p.in.pds.py", args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        self._runner.log_line.connect(self._append_log)
        self._runner.progress.connect(self._set_progress)
        self._runner.finished.connect(self._on_done)
        self._runner.start()

    def _on_done(self, ok, msg):
        self._run_btn.setEnabled(True)
        if ok:
            self._append_log("✓ DEM imported successfully.")
            self._done = True
            self.completeChanged.emit()
        else:
            self._append_log(f"✗ Import failed: {msg}")

    def isComplete(self):
        return self._done


class TerrainPage(QWizardPage, LogMixin):
    """Page 3 — Terrain analysis (slope, roughness, hazard, ellipse)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 3 — Terrain Analysis")
        self.setSubTitle("Multi-scale slope, RMS roughness, Moran's I, "
                         "hazard map, and landing-ellipse scan.")
        self._done = False
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)

        tabs = QTabWidget()

        # ── Slope tab
        slope_w = QWidget(); sf = QFormLayout(slope_w)
        self._scales     = QLineEdit(self.session.get("scales",     "5,50,500"))
        self._thresholds = QLineEdit(self.session.get("thresholds", "15,10,7"))
        sf.addRow("Scales (m, comma-sep):", self._scales)
        sf.addRow("Thresholds (°, per scale):", self._thresholds)
        tabs.addTab(slope_w, "Slope")

        # ── Roughness tab
        rough_w = QWidget(); rf = QFormLayout(rough_w)
        self._win_size  = QSpinBox(); self._win_size.setRange(3, 101)
        self._win_size.setSingleStep(2)
        self._win_size.setValue(int(self.session.get("win_size", 11)))
        self._rms_thr   = QDoubleSpinBox(); self._rms_thr.setRange(0.01, 100)
        self._rms_thr.setSingleStep(0.1)
        self._rms_thr.setValue(float(self.session.get("rms_thr", 0.5)))
        rf.addRow("Window (pixels, odd):", self._win_size)
        rf.addRow("RMS threshold (m):", self._rms_thr)
        tabs.addTab(rough_w, "Roughness")

        # ── Ellipse tab
        ell_w = QWidget(); ef = QFormLayout(ell_w)
        self._ell_major = QDoubleSpinBox(); self._ell_major.setRange(100, 1e6)
        self._ell_major.setValue(float(self.session.get("ell_major", 30000)))
        self._ell_minor = QDoubleSpinBox(); self._ell_minor.setRange(100, 1e6)
        self._ell_minor.setValue(float(self.session.get("ell_minor", 15000)))
        self._scan_res  = QDoubleSpinBox(); self._scan_res.setRange(10, 10000)
        self._scan_res.setValue(float(self.session.get("scan_res",  500)))
        ef.addRow("Ellipse major axis (m):", self._ell_major)
        ef.addRow("Ellipse minor axis (m):", self._ell_minor)
        ef.addRow("Scan resolution (m):",    self._scan_res)
        tabs.addTab(ell_w, "Ellipse scan")

        lay.addWidget(tabs)

        self._run_btn = QPushButton("▶  Run terrain analysis")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)
        lay.addWidget(self._build_log_panel())

    def _run(self):
        self._clear_log(); self._done = False
        self._run_btn.setEnabled(False)
        dem = self.session.get("dem_name", "lola_dem")
        self._save_params()
        self._step = 0
        self._steps = [
            ("p.terrain.slope/p.terrain.slope.py",
             [f"dem={dem}",
              f"scales={self._scales.text()}",
              f"thresholds={self._thresholds.text()}",
              "prefix=slope", "--overwrite"]),
            ("p.terrain.roughness/p.terrain.roughness.py",
             [f"dem={dem}",
              f"window={self._win_size.value()}",
              f"threshold={self._rms_thr.value()}",
              "prefix=roughness", "--overwrite"]),
            ("p.terrain.hazard/p.terrain.hazard.py",
             [f"dem={dem}",
              "slope=slope_5m", "roughness=roughness_rms",
              "prefix=hazard", "--overwrite"]),
            ("p.terrain.ellipse/p.terrain.ellipse.py",
             [f"dem={dem}",
              f"ellipse_major={int(self._ell_major.value())}",
              f"ellipse_minor={int(self._ell_minor.value())}",
              f"scan_res={int(self._scan_res.value())}",
              "prefix=ellipse", "--overwrite"]),
        ]
        self._run_next()

    def _run_next(self):
        if self._step >= len(self._steps):
            self._append_log("✓ All terrain steps complete.")
            self._done = True
            self._run_btn.setEnabled(True)
            self.completeChanged.emit()
            return
        script, args = self._steps[self._step]
        self._append_log(f"\n── {script.split('/')[0]} ──")
        runner = PythonScriptRunner(script, args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        runner.log_line.connect(self._append_log)
        runner.progress.connect(self._set_progress)
        runner.finished.connect(self._step_done)
        self._current_runner = runner
        runner.start()

    def _step_done(self, ok, msg):
        if not ok:
            self._append_log(f"✗ Step failed: {msg}")
            self._run_btn.setEnabled(True)
            return
        self._step += 1
        self._run_next()

    def _save_params(self):
        self.session.set("scales",     self._scales.text())
        self.session.set("thresholds", self._thresholds.text())
        self.session.set("win_size",   self._win_size.value())
        self.session.set("rms_thr",    self._rms_thr.value())
        self.session.set("ell_major",  self._ell_major.value())
        self.session.set("ell_minor",  self._ell_minor.value())
        self.session.set("scan_res",   self._scan_res.value())

    def isComplete(self):
        return self._done


class IlluminationPage(QWizardPage, LogMixin):
    """Page 4 — Solar illumination (p.illumination.sunfraction + shadow)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 4 — Illumination Analysis")
        self.setSubTitle("Time-averaged illumination fraction, PSR mask, "
                         "and shadow frequency (uses p.sunmask for acceleration).")
        self._done = False
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)
        form = QFormLayout()

        self._nsteps = QSpinBox(); self._nsteps.setRange(4, 360)
        self._nsteps.setValue(int(self.session.get("illum_nsteps", 36)))
        self._nsteps.setToolTip("36 = quick test, 360 = production")
        form.addRow("Time steps:", self._nsteps)

        self._sunmask = QComboBox()
        self._sunmask.addItems(["p.sunmask (OpenCL+OpenMP)", "r.sunmask (serial)"])
        self._sunmask.setCurrentIndex(0 if self.session.get("sunmask", "p.sunmask") == "p.sunmask" else 1)
        form.addRow("Shadow module:", self._sunmask)

        self._shad_thr = QDoubleSpinBox(); self._shad_thr.setRange(0, 1)
        self._shad_thr.setSingleStep(0.05)
        self._shad_thr.setValue(float(self.session.get("shad_thr", 0.70)))
        form.addRow("Shadow hazard threshold:", self._shad_thr)

        lay.addLayout(form)

        self._run_btn = QPushButton("▶  Run illumination analysis")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)
        lay.addWidget(self._build_log_panel())

    def _run(self):
        self._clear_log(); self._done = False
        self._run_btn.setEnabled(False)
        dem  = self.session.get("dem_name", "lola_dem")
        body = self.field("body")
        smmod = "p.sunmask" if self._sunmask.currentIndex() == 0 else "r.sunmask"
        self.session.set("illum_nsteps", self._nsteps.value())
        self.session.set("sunmask",      smmod)
        self.session.set("shad_thr",     self._shad_thr.value())
        self._step = 0
        self._steps = [
            ("p.illumination.sunfraction/p.illumination.sunfraction.py",
             [f"dem={dem}", f"body={body}",
              f"nsteps={self._nsteps.value()}",
              f"sunmask_module={smmod}",
              "prefix=illum", "--overwrite"]),
            ("p.illumination.shadow/p.illumination.shadow.py",
             [f"dem={dem}", f"body={body}",
              f"nsteps={self._nsteps.value()}",
              f"sunmask_module={smmod}",
              f"shadow_threshold={self._shad_thr.value()}",
              "prefix=shadow", "--overwrite"]),
        ]
        self._run_next()

    def _run_next(self):
        if self._step >= len(self._steps):
            self._append_log("✓ Illumination analysis complete.")
            self._done = True; self._run_btn.setEnabled(True)
            self.completeChanged.emit(); return
        script, args = self._steps[self._step]
        self._append_log(f"\n── {script.split('/')[0]} ──")
        r = PythonScriptRunner(script, args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        r.log_line.connect(self._append_log)
        r.progress.connect(self._set_progress)
        r.finished.connect(self._step_done)
        self._runner = r; r.start()

    def _step_done(self, ok, msg):
        if not ok:
            self._append_log(f"✗ {msg}"); self._run_btn.setEnabled(True); return
        self._step += 1; self._run_next()

    def isComplete(self): return self._done


class VisibilityPage(QWizardPage, LogMixin):
    """Page 5 — Visibility (Earth, LOS horizon, orbiter)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 5 — Visibility Analysis")
        self.setSubTitle("Earth/relay visibility fraction, terrain horizon masking, "
                         "and orbiter contact fraction.")
        self._done = False
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)
        form = QFormLayout()

        self._vis_nsteps = QSpinBox(); self._vis_nsteps.setRange(4, 360)
        self._vis_nsteps.setValue(int(self.session.get("vis_nsteps", 36)))
        form.addRow("Time steps (Earth vis):", self._vis_nsteps)

        self._min_elev = QDoubleSpinBox(); self._min_elev.setRange(0, 30)
        self._min_elev.setValue(float(self.session.get("min_elev", 3.0)))
        form.addRow("Min Earth elevation (°):", self._min_elev)

        self._hor_step = QDoubleSpinBox(); self._hor_step.setRange(5, 90)
        self._hor_step.setValue(float(self.session.get("hor_step", 22.5)))
        form.addRow("Horizon angular step (°):", self._hor_step)

        self._scan_res_vis = QDoubleSpinBox(); self._scan_res_vis.setRange(10, 10000)
        self._scan_res_vis.setValue(float(self.session.get("scan_res_vis", 50)))
        self._scan_res_vis.setToolTip("Coarsen DEM for r.horizon computation")
        form.addRow("Horizon scan resolution (m):", self._scan_res_vis)

        self._ndirs = QSpinBox(); self._ndirs.setRange(4, 36)
        self._ndirs.setValue(int(self.session.get("ndirs", 16)))
        form.addRow("LOS horizon directions:", self._ndirs)

        lay.addLayout(form)
        self._run_btn = QPushButton("▶  Run visibility analysis")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)
        lay.addWidget(self._build_log_panel())

    def _run(self):
        self._clear_log(); self._done = False
        self._run_btn.setEnabled(False)
        dem  = self.session.get("dem_name", "lola_dem")
        body = self.field("body")
        self.session.set("vis_nsteps",   self._vis_nsteps.value())
        self.session.set("min_elev",     self._min_elev.value())
        self.session.set("hor_step",     self._hor_step.value())
        self.session.set("scan_res_vis", self._scan_res_vis.value())
        self.session.set("ndirs",        self._ndirs.value())
        self._step = 0
        self._steps = [
            ("p.visibility.earth/p.visibility.earth.py",
             [f"dem={dem}", f"body={body}",
              f"nsteps={self._vis_nsteps.value()}",
              f"min_elevation={self._min_elev.value()}",
              f"horizon_step={self._hor_step.value()}",
              "prefix=earth_vis", "--overwrite"]),
            ("p.visibility.los/p.visibility.los.py",
             [f"dem={dem}",
              f"directions={self._ndirs.value()}",
              f"scan_res={self._scan_res_vis.value()}",
              "prefix=los", "--overwrite"]),
        ]
        self._run_next()

    def _run_next(self):
        if self._step >= len(self._steps):
            self._append_log("✓ Visibility analysis complete.")
            self._done = True; self._run_btn.setEnabled(True)
            self.completeChanged.emit(); return
        script, args = self._steps[self._step]
        self._append_log(f"\n── {script.split('/')[0]} ──")
        r = PythonScriptRunner(script, args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        r.log_line.connect(self._append_log)
        r.progress.connect(self._set_progress)
        r.finished.connect(self._step_done)
        self._runner = r; r.start()

    def _step_done(self, ok, msg):
        if not ok:
            self._append_log(f"✗ {msg}"); self._run_btn.setEnabled(True); return
        self._step += 1; self._run_next()

    def isComplete(self): return self._done


class MCDMPage(QWizardPage, LogMixin):
    """Page 6 — MCDM scoring (weights, WLC, TOPSIS)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 6 — Multi-Criteria Decision Making")
        self.setSubTitle("Define criterion weights and compute weighted "
                         "suitability scores (WLC and/or TOPSIS).")
        self._done = False
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)

        grp = QGroupBox("Criterion weights  (must sum to 1.0)")
        wlay = QFormLayout(grp)

        default_w = self.session.get("weights",
            {"slope": 0.25, "roughness": 0.15,
             "illumination": 0.20, "earth_vis": 0.15, "science": 0.25})
        self._w = {}
        for crit in ["slope", "roughness", "illumination", "earth_vis", "science"]:
            sb = QDoubleSpinBox(); sb.setRange(0, 1); sb.setSingleStep(0.05)
            sb.setValue(float(default_w.get(crit, 0.2)))
            self._w[crit] = sb
            wlay.addRow(f"{crit.capitalize()}:", sb)
        lay.addWidget(grp)

        grp2 = QGroupBox("Method")
        mlay = QFormLayout(grp2)
        self._method = QComboBox()
        self._method.addItems(["Both WLC and TOPSIS", "WLC only", "TOPSIS only"])
        mlay.addRow("Scoring method:", self._method)
        lay.addWidget(grp2)

        self._run_btn = QPushButton("▶  Run MCDM scoring")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)
        lay.addWidget(self._build_log_panel())

    def _run(self):
        self._clear_log(); self._done = False
        self._run_btn.setEnabled(False)

        w = {k: v.value() for k, v in self._w.items()}
        total = sum(w.values())
        if abs(total - 1.0) > 0.01:
            self._append_log(f"WARNING: weights sum to {total:.3f}, not 1.0 — normalising.")
            w = {k: v/total for k, v in w.items()}
        self.session.set("weights", w)

        wstr = ",".join(str(w[k]) for k in
                        ["slope","roughness","illumination","earth_vis","science"])
        methods = {"Both WLC and TOPSIS": "both",
                   "WLC only": "wlc",
                   "TOPSIS only": "topsis"}
        method = methods[self._method.currentText()]

        args = [
            "slope=hazard_composite",
            "roughness=roughness_rms",
            "illumination=illum_fraction",
            "earth_vis=earth_vis_fraction",
            "exclusion_masks=hazard_mask",
            f"weights={wstr}",
            f"method={method}",
            "prefix=suitability",
            "--overwrite",
        ]
        r = PythonScriptRunner("p.mcdm.score/p.mcdm.score.py", args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        r.log_line.connect(self._append_log)
        r.progress.connect(self._set_progress)
        r.finished.connect(self._on_done)
        self._runner = r; r.start()

    def _on_done(self, ok, msg):
        self._run_btn.setEnabled(True)
        if ok:
            self._append_log("✓ MCDM scoring complete.")
            self._done = True; self.completeChanged.emit()
        else:
            self._append_log(f"✗ {msg}")

    def isComplete(self): return self._done


class RankPage(QWizardPage, LogMixin):
    """Page 7 — Candidate ranking + results table."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.setTitle("Step 7 — Ranking & Results")
        self.setSubTitle("Extract top candidate landing sites, compute Monte Carlo "
                         "weight sensitivity, and export a JSON report.")
        self._done = False
        self._report_path = str(
            Path.home() / "p_landing_report.json")
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)

        form = QFormLayout()
        self._min_area = QDoubleSpinBox(); self._min_area.setRange(0.001, 1000)
        self._min_area.setSingleStep(0.1)
        self._min_area.setValue(float(self.session.get("min_area", 0.5)))
        self._min_area.setSuffix(" km²")
        form.addRow("Minimum candidate area:", self._min_area)

        self._top_pct = QDoubleSpinBox(); self._top_pct.setRange(10, 95)
        self._top_pct.setValue(float(self.session.get("top_pct", 50)))
        self._top_pct.setSuffix("%")
        form.addRow("Suitability percentile threshold:", self._top_pct)

        self._n_cand = QSpinBox(); self._n_cand.setRange(1, 50)
        self._n_cand.setValue(int(self.session.get("n_cand", 10)))
        form.addRow("Max candidates to report:", self._n_cand)

        self._mc = QSpinBox(); self._mc.setRange(0, 2000)
        self._mc.setValue(int(self.session.get("mc_samples", 200)))
        form.addRow("Monte Carlo samples:", self._mc)
        lay.addLayout(form)

        self._run_btn = QPushButton("▶  Run ranking")
        self._run_btn.setFixedHeight(36)
        self._run_btn.clicked.connect(self._run)
        lay.addWidget(self._run_btn)
        lay.addWidget(self._build_log_panel())

        # Results table
        self._table = QTableWidget(0, 5)
        self._table.setHorizontalHeaderLabels(
            ["Rank", "Mean suitability", "Std dev", "Area (km²)", "P(rank=1)"])
        self._table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch)
        self._table.setMinimumHeight(150)
        lay.addWidget(self._table)

    def _run(self):
        self._clear_log(); self._done = False
        self._run_btn.setEnabled(False)
        self.session.set("min_area",   self._min_area.value())
        self.session.set("top_pct",    self._top_pct.value())
        self.session.set("n_cand",     self._n_cand.value())
        self.session.set("mc_samples", self._mc.value())

        args = [
            "suitability=suitability_wlc",
            "criteria=hazard_composite,roughness_rms,illum_fraction,earth_vis_fraction",
            f"min_area_km2={self._min_area.value()}",
            f"top_percentile={self._top_pct.value()}",
            f"n_candidates={self._n_cand.value()}",
            f"mc_samples={self._mc.value()}",
            "prefix=rank",
            f"report={self._report_path}",
            "--overwrite",
        ]
        r = PythonScriptRunner("p.rank/p.rank.py", args,
            self.field("gisdb"), self.field("location"), self.field("mapset"))
        r.log_line.connect(self._append_log)
        r.progress.connect(self._set_progress)
        r.finished.connect(self._on_done)
        self._runner = r; r.start()

    def _on_done(self, ok, msg):
        self._run_btn.setEnabled(True)
        if ok:
            self._append_log("✓ Ranking complete.")
            self._load_results()
            self._done = True; self.completeChanged.emit()
        else:
            self._append_log(f"✗ {msg}")

    def _load_results(self):
        try:
            with open(self._report_path) as f:
                data = json.load(f)
            candidates = data.get("candidates", [])
            self._table.setRowCount(len(candidates))
            for i, c in enumerate(candidates):
                self._table.setItem(i, 0, QTableWidgetItem(str(c["rank"])))
                self._table.setItem(i, 1, QTableWidgetItem(f"{c['suit_mean']:.4f}"))
                self._table.setItem(i, 2, QTableWidgetItem(f"{c['suit_std']:.4f}"))
                self._table.setItem(i, 3, QTableWidgetItem(f"{c['area_km2']:.2f}"))
                p1 = c.get("rank1_probability")
                self._table.setItem(i, 4, QTableWidgetItem(
                    f"{p1:.3f}" if p1 is not None else "—"))
            self._append_log(f"\nReport saved: {self._report_path}")
        except Exception as e:
            self._append_log(f"Could not load report: {e}")

    def isComplete(self): return self._done


# ═══════════════════════════════════════════════════════════════════════════
# Main wizard
# ═══════════════════════════════════════════════════════════════════════════

class LandingWizard(QWizard):
    """Planetary Landing Site Evaluation — Qt6 wizard."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session

        self.setWindowTitle("Planetary Landing Site Evaluation Wizard")
        self.setWizardStyle(QWizard.WizardStyle.ModernStyle)
        self.setMinimumSize(QSize(900, 700))
        self.setOption(QWizard.WizardOption.HaveHelpButton, False)
        self.setOption(QWizard.WizardOption.NoCancelButtonOnLastPage, True)

        self.addPage(SetupPage(session))
        self.addPage(ImportPage(session))
        self.addPage(TerrainPage(session))
        self.addPage(IlluminationPage(session))
        self.addPage(VisibilityPage(session))
        self.addPage(MCDMPage(session))
        self.addPage(RankPage(session))

        self.setButtonText(QWizard.WizardButton.FinishButton, "Save & Close")
        self.finished.connect(self._on_finish)

    def _on_finish(self, result):
        self.session.save()


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Planetary Landing Site Evaluation Qt6 Wizard")
    parser.add_argument("--session", default=None,
                        help="Path to session JSON file")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("P.Landing Wizard")
    app.setOrganizationName("GRASS GIS Planetary")

    if not GRASS_OK:
        from PyQt6.QtWidgets import QMessageBox
        QMessageBox.critical(None, "GRASS not found",
                             f"Cannot locate GRASS GIS:\n{_GRASS_ERR}\n\n"
                             "Set GISBASE or install GRASS GIS.")
        sys.exit(1)

    session = Session(args.session)
    wiz = LandingWizard(session)
    wiz.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
