"""
landing_wizard_wx.py — wxPython wizard for GRASS wxGUI integration.

Called by g.gui.landing; also runnable standalone:
    python3 landing_wizard_wx.py

License: Unlicense (https://unlicense.org)
"""

import os
import sys
import json
import subprocess
import threading
from pathlib import Path

try:
    import wx
    import wx.adv
    WX_OK = True
except ImportError:
    WX_OK = False

import grass.script as gs

SRCDIR = Path(__file__).parent.parent


# ── GRASS runner (thread-based for wx) ───────────────────────────────────────

class GrassThread(threading.Thread):
    """Run a Python p.* script; post log events to a wx window."""

    def __init__(self, script, args, env, log_win, done_cb):
        super().__init__(daemon=True)
        self.cmd     = ["python3", str(SRCDIR / script)] + args
        self.env     = env
        self.log_win = log_win
        self.done_cb = done_cb

    def run(self):
        try:
            proc = subprocess.Popen(
                self.cmd, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, env=self.env)
            for line in proc.stdout:
                wx.CallAfter(self._log, line.rstrip())
            proc.wait()
            ok = proc.returncode == 0
            wx.CallAfter(self.done_cb, ok)
        except Exception as e:
            wx.CallAfter(self._log, f"ERROR: {e}")
            wx.CallAfter(self.done_cb, False)

    def _log(self, text):
        self.log_win.AppendText(text + "\n")
        self.log_win.ShowPosition(self.log_win.GetLastPosition())


def _make_env(gisdb, location, mapset):
    import tempfile
    env = os.environ.copy()
    gisbase = env.get("GISBASE", "/usr/local/grass86")
    fd, gisrc = tempfile.mkstemp(suffix=".gisrc", prefix="planding_wx_")
    os.close(fd)
    with open(gisrc, "w") as f:
        f.write(f"GISDBASE: {gisdb}\n"
                f"LOCATION_NAME: {location}\n"
                f"MAPSET: {mapset}\n")
    env["GISRC"]     = gisrc
    env["GISBASE"]   = gisbase
    env["GISDBASE"]  = gisdb
    env["PATH"]      = (os.path.join(gisbase, "bin") + os.pathsep +
                        os.path.join(gisbase, "scripts") + os.pathsep +
                        env.get("PATH", ""))
    env["LD_LIBRARY_PATH"] = (os.path.join(gisbase, "lib") + os.pathsep +
                               env.get("LD_LIBRARY_PATH", ""))
    return env, gisrc


# ── Shared log panel helper ──────────────────────────────────────────────────

def _make_log_panel(parent):
    """Return (panel, log_ctrl, gauge) packed in a wx.Panel."""
    pnl  = wx.Panel(parent)
    sizer = wx.BoxSizer(wx.VERTICAL)
    log  = wx.TextCtrl(pnl, style=wx.TE_MULTILINE | wx.TE_READONLY |
                                   wx.TE_RICH2 | wx.HSCROLL)
    log.SetFont(wx.Font(9, wx.FONTFAMILY_TELETYPE,
                        wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))
    log.SetMinSize((-1, 120))
    gauge = wx.Gauge(pnl, range=100)
    sizer.Add(log,   1, wx.EXPAND | wx.ALL, 2)
    sizer.Add(gauge, 0, wx.EXPAND | wx.ALL, 2)
    pnl.SetSizer(sizer)
    return pnl, log, gauge


# ══════════════════════════════════════════════════════════════════════════
# Wizard pages (wx.adv.WizardPageSimple)
# ══════════════════════════════════════════════════════════════════════════

class SetupWxPage(wx.adv.WizardPageSimple):
    def __init__(self, parent, session):
        super().__init__(parent)
        self.session = session
        sizer = wx.BoxSizer(wx.VERTICAL)

        grp  = wx.StaticBox(self, label="GRASS GIS environment")
        gsz  = wx.StaticBoxSizer(grp, wx.VERTICAL)
        form = wx.FlexGridSizer(3, 3, 4, 8)
        form.AddGrowableCol(1, 1)

        self._gisdb = wx.TextCtrl(self, value=session.get(
            "gisdb", str(Path.home() / "grassdata")))
        btn_db = wx.Button(self, label="Browse…", size=(80, -1))
        btn_db.Bind(wx.EVT_BUTTON, lambda e: self._pick(self._gisdb, True))
        form.Add(wx.StaticText(self, label="Database:"), 0, wx.ALIGN_CENTER_VERTICAL)
        form.Add(self._gisdb, 1, wx.EXPAND); form.Add(btn_db)

        self._loc = wx.TextCtrl(self, value=session.get("location", "Moon_SouthPole_5m"))
        form.Add(wx.StaticText(self, label="Location:"), 0, wx.ALIGN_CENTER_VERTICAL)
        form.Add(self._loc, 1, wx.EXPAND); form.AddSpacer(0)

        self._mapset = wx.TextCtrl(self, value=session.get("mapset", "PERMANENT"))
        form.Add(wx.StaticText(self, label="Mapset:"), 0, wx.ALIGN_CENTER_VERTICAL)
        form.Add(self._mapset, 1, wx.EXPAND); form.AddSpacer(0)

        gsz.Add(form, 0, wx.EXPAND | wx.ALL, 6)
        sizer.Add(gsz, 0, wx.EXPAND | wx.ALL, 8)

        grp2 = wx.StaticBox(self, label="Body & mission")
        gsz2 = wx.StaticBoxSizer(grp2, wx.VERTICAL)
        form2 = wx.FlexGridSizer(2, 3, 4, 8)
        form2.AddGrowableCol(1, 1)

        self._body = wx.TextCtrl(self, value=session.get(
            "body", str(SRCDIR / "bodies" / "moon.json")))
        btn_body = wx.Button(self, label="Browse…", size=(80, -1))
        btn_body.Bind(wx.EVT_BUTTON, lambda e: self._pick(self._body, False))
        form2.Add(wx.StaticText(self, label="Body JSON:"), 0, wx.ALIGN_CENTER_VERTICAL)
        form2.Add(self._body, 1, wx.EXPAND); form2.Add(btn_body)

        self._mission = wx.TextCtrl(self, value=session.get(
            "mission", str(SRCDIR / "missions" / "luna27.json")))
        btn_m = wx.Button(self, label="Browse…", size=(80, -1))
        btn_m.Bind(wx.EVT_BUTTON, lambda e: self._pick(self._mission, False))
        form2.Add(wx.StaticText(self, label="Mission JSON:"), 0, wx.ALIGN_CENTER_VERTICAL)
        form2.Add(self._mission, 1, wx.EXPAND); form2.Add(btn_m)

        gsz2.Add(form2, 0, wx.EXPAND | wx.ALL, 6)
        sizer.Add(gsz2, 0, wx.EXPAND | wx.ALL, 8)

        self.SetSizer(sizer)
        self.Bind(wx.adv.EVT_WIZARD_PAGE_CHANGING, self._on_next)

    def _pick(self, ctrl, is_dir):
        if is_dir:
            dlg = wx.DirDialog(self, "Select GRASS database", ctrl.GetValue())
            if dlg.ShowModal() == wx.ID_OK:
                ctrl.SetValue(dlg.GetPath())
        else:
            dlg = wx.FileDialog(self, "Select JSON", wildcard="JSON (*.json)|*.json")
            if dlg.ShowModal() == wx.ID_OK:
                ctrl.SetValue(dlg.GetPath())

    def _on_next(self, event):
        if event.GetDirection():   # moving forward
            self.session.set("gisdb",    self._gisdb.GetValue())
            self.session.set("location", self._loc.GetValue())
            self.session.set("mapset",   self._mapset.GetValue())
            self.session.set("body",     self._body.GetValue())
            self.session.set("mission",  self._mission.GetValue())


class RunStepWxPage(wx.adv.WizardPageSimple):
    """Generic reusable page: parameters form + Run button + log + gauge."""

    def __init__(self, parent, session, title, subtitle, build_form_cb, build_cmd_cb):
        super().__init__(parent)
        self.session       = session
        self._build_cmd_cb = build_cmd_cb
        self._done         = False

        sizer = wx.BoxSizer(wx.VERTICAL)

        hdr = wx.StaticText(self, label=title)
        hdr.SetFont(wx.Font(11, wx.FONTFAMILY_DEFAULT,
                            wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD))
        sizer.Add(hdr, 0, wx.ALL, 8)
        sizer.Add(wx.StaticText(self, label=subtitle), 0, wx.LEFT | wx.BOTTOM, 8)

        # Form (built by caller)
        self._form_panel = wx.Panel(self)
        form_sizer = wx.BoxSizer(wx.VERTICAL)
        build_form_cb(self._form_panel, form_sizer)
        self._form_panel.SetSizer(form_sizer)
        sizer.Add(self._form_panel, 0, wx.EXPAND | wx.ALL, 4)

        self._run_btn = wx.Button(self, label="▶  Run")
        self._run_btn.SetFont(wx.Font(10, wx.FONTFAMILY_DEFAULT,
                                      wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD))
        self._run_btn.Bind(wx.EVT_BUTTON, self._on_run)
        sizer.Add(self._run_btn, 0, wx.EXPAND | wx.ALL, 4)

        log_pnl, self._log, self._gauge = _make_log_panel(self)
        sizer.Add(log_pnl, 1, wx.EXPAND | wx.ALL, 4)

        self.SetSizer(sizer)
        self.Bind(wx.adv.EVT_WIZARD_PAGE_CHANGING, self._on_next)

    def _on_run(self, event):
        self._log.Clear(); self._gauge.SetValue(0)
        self._done = False
        self._run_btn.Enable(False)
        steps = self._build_cmd_cb()
        self._steps = steps; self._step_idx = 0
        self._run_next()

    def _run_next(self):
        if self._step_idx >= len(self._steps):
            self._log.AppendText("✓ Done.\n")
            self._done = True
            self._run_btn.Enable(True)
            return
        script, args = self._steps[self._step_idx]
        self._log.AppendText(f"\n── {script.split('/')[0]} ──\n")
        env, gisrc = _make_env(
            self.session.get("gisdb",    ""),
            self.session.get("location", ""),
            self.session.get("mapset",   "PERMANENT"))
        self._gisrc = gisrc
        t = GrassThread(script, args, env, self._log, self._on_step_done)
        t.start()

    def _on_step_done(self, ok):
        try:
            os.unlink(self._gisrc)
        except OSError:
            pass
        if ok:
            self._step_idx += 1
            self._run_next()
        else:
            self._log.AppendText("✗ Step failed.\n")
            self._run_btn.Enable(True)

    def _on_next(self, event):
        if event.GetDirection() and not self._done:
            wx.MessageBox("Please run this step before continuing.",
                          "Not complete", wx.OK | wx.ICON_WARNING)
            event.Veto()


# ══════════════════════════════════════════════════════════════════════════
# Wizard assembly
# ══════════════════════════════════════════════════════════════════════════

class LandingWxWizard(wx.adv.Wizard):
    def __init__(self, parent, session):
        super().__init__(parent, title="Planetary Landing Site Evaluation",
                         style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER)
        self.session = session
        self.SetPageSize((880, 640))

        p1 = SetupWxPage(self, session)

        # ── Page 2: DEM import
        def _build_import_form(panel, sizer):
            form = wx.FlexGridSizer(2, 2, 4, 8); form.AddGrowableCol(1, 1)
            self._dem_file = wx.TextCtrl(panel, value=session.get("dem_file", ""))
            btn = wx.Button(panel, label="Browse…", size=(80,-1))
            btn.Bind(wx.EVT_BUTTON, lambda e: self._pick_dem())
            form.Add(wx.StaticText(panel, label="DEM file:"), 0, wx.ALIGN_CENTER_VERTICAL)
            row = wx.BoxSizer(wx.HORIZONTAL)
            row.Add(self._dem_file, 1, wx.EXPAND)
            row.Add(btn, 0, wx.LEFT, 4)
            form.Add(row, 1, wx.EXPAND)
            self._dem_name = wx.TextCtrl(panel, value=session.get("dem_name","lola_dem"))
            form.Add(wx.StaticText(panel, label="Output map:"), 0, wx.ALIGN_CENTER_VERTICAL)
            form.Add(self._dem_name, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_import_cmd():
            session.set("dem_file", self._dem_file.GetValue())
            session.set("dem_name", self._dem_name.GetValue())
            return [("p.in.pds/p.in.pds.py",
                     [f"input={self._dem_file.GetValue()}",
                      f"output={self._dem_name.GetValue()}",
                      "--overwrite"])]

        p2 = RunStepWxPage(self, session,
            "Step 2 — DEM Import",
            "Import the planetary DEM (PDS3/PDS4/GeoTIFF) into the GRASS mapset.",
            _build_import_form, _build_import_cmd)

        # ── Page 3: Terrain
        def _build_terrain_form(panel, sizer):
            form = wx.FlexGridSizer(5, 2, 4, 8); form.AddGrowableCol(1,1)
            self._scales = wx.TextCtrl(panel, value=session.get("scales","5,50,500"))
            self._thresholds = wx.TextCtrl(panel, value=session.get("thresholds","15,10,7"))
            self._win_size = wx.SpinCtrl(panel, min=3, max=101,
                                          value=str(session.get("win_size",11)))
            self._rms_thr = wx.SpinCtrlDouble(panel, min=0.01, max=100,
                                               value=str(session.get("rms_thr",0.5)), inc=0.1)
            self._scan_res = wx.SpinCtrlDouble(panel, min=10, max=10000,
                                               value=str(session.get("scan_res",500)))
            for label, ctrl in [
                ("Scales (m):", self._scales), ("Thresholds (°):", self._thresholds),
                ("Roughness window (px):", self._win_size),
                ("RMS threshold (m):", self._rms_thr),
                ("Ellipse scan res (m):", self._scan_res)]:
                form.Add(wx.StaticText(panel, label=label), 0, wx.ALIGN_CENTER_VERTICAL)
                form.Add(ctrl, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_terrain_cmd():
            dem = session.get("dem_name","lola_dem")
            session.set("scales",     self._scales.GetValue())
            session.set("thresholds", self._thresholds.GetValue())
            session.set("win_size",   self._win_size.GetValue())
            session.set("rms_thr",    self._rms_thr.GetValue())
            session.set("scan_res",   self._scan_res.GetValue())
            return [
                ("p.terrain.slope/p.terrain.slope.py",
                 [f"dem={dem}", f"scales={self._scales.GetValue()}",
                  f"thresholds={self._thresholds.GetValue()}",
                  "prefix=slope", "--overwrite"]),
                ("p.terrain.roughness/p.terrain.roughness.py",
                 [f"dem={dem}", f"window={self._win_size.GetValue()}",
                  f"threshold={self._rms_thr.GetValue()}",
                  "prefix=roughness", "--overwrite"]),
                ("p.terrain.hazard/p.terrain.hazard.py",
                 [f"dem={dem}", "slope=slope_5m", "roughness=roughness_rms",
                  "prefix=hazard", "--overwrite"]),
                ("p.terrain.ellipse/p.terrain.ellipse.py",
                 [f"dem={dem}", f"scan_res={int(self._scan_res.GetValue())}",
                  "prefix=ellipse", "--overwrite"]),
            ]
        p3 = RunStepWxPage(self, session,
            "Step 3 — Terrain Analysis",
            "Multi-scale slope, roughness, hazard map, ellipse scan.",
            _build_terrain_form, _build_terrain_cmd)

        # ── Page 4: Illumination
        def _build_illum_form(panel, sizer):
            form = wx.FlexGridSizer(3, 2, 4, 8); form.AddGrowableCol(1,1)
            self._illum_n = wx.SpinCtrl(panel, min=4, max=360,
                                         value=str(session.get("illum_nsteps",36)))
            self._sunmask_wx = wx.Choice(panel, choices=["p.sunmask","r.sunmask"])
            self._sunmask_wx.SetSelection(0)
            self._shad_thr_wx = wx.SpinCtrlDouble(panel, min=0, max=1,
                value=str(session.get("shad_thr",0.7)), inc=0.05)
            for lbl, ctrl in [("Steps:", self._illum_n),
                               ("Shadow module:", self._sunmask_wx),
                               ("Shadow threshold:", self._shad_thr_wx)]:
                form.Add(wx.StaticText(panel, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
                form.Add(ctrl, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_illum_cmd():
            dem  = session.get("dem_name","lola_dem")
            body = session.get("body","")
            smmod = self._sunmask_wx.GetString(self._sunmask_wx.GetSelection())
            session.set("illum_nsteps", self._illum_n.GetValue())
            session.set("sunmask",      smmod)
            session.set("shad_thr",     self._shad_thr_wx.GetValue())
            base = [f"dem={dem}", f"body={body}",
                    f"nsteps={self._illum_n.GetValue()}",
                    f"sunmask_module={smmod}", "--overwrite"]
            return [
                ("p.illumination.sunfraction/p.illumination.sunfraction.py",
                 base + ["prefix=illum"]),
                ("p.illumination.shadow/p.illumination.shadow.py",
                 base + ["prefix=shadow",
                         f"shadow_threshold={self._shad_thr_wx.GetValue()}"]),
            ]
        p4 = RunStepWxPage(self, session,
            "Step 4 — Illumination",
            "Time-averaged illumination fraction and shadow frequency.",
            _build_illum_form, _build_illum_cmd)

        # ── Page 5: Visibility
        def _build_vis_form(panel, sizer):
            form = wx.FlexGridSizer(4, 2, 4, 8); form.AddGrowableCol(1,1)
            self._vis_n    = wx.SpinCtrl(panel, min=4, max=360,
                                          value=str(session.get("vis_nsteps",36)))
            self._vis_elev = wx.SpinCtrlDouble(panel, min=0, max=30,
                                                value=str(session.get("min_elev",3)))
            self._vis_hs   = wx.SpinCtrlDouble(panel, min=5, max=90,
                                                value=str(session.get("hor_step",22.5)))
            self._vis_sres = wx.SpinCtrlDouble(panel, min=10, max=10000,
                                                value=str(session.get("scan_res_vis",50)))
            for lbl, ctrl in [("Earth vis steps:", self._vis_n),
                               ("Min Earth elev (°):", self._vis_elev),
                               ("Horizon step (°):", self._vis_hs),
                               ("Horizon scan res (m):", self._vis_sres)]:
                form.Add(wx.StaticText(panel, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
                form.Add(ctrl, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_vis_cmd():
            dem  = session.get("dem_name","lola_dem")
            body = session.get("body","")
            session.set("vis_nsteps",   self._vis_n.GetValue())
            session.set("min_elev",     self._vis_elev.GetValue())
            session.set("hor_step",     self._vis_hs.GetValue())
            session.set("scan_res_vis", self._vis_sres.GetValue())
            return [
                ("p.visibility.earth/p.visibility.earth.py",
                 [f"dem={dem}", f"body={body}",
                  f"nsteps={self._vis_n.GetValue()}",
                  f"min_elevation={self._vis_elev.GetValue()}",
                  f"horizon_step={self._vis_hs.GetValue()}",
                  "prefix=earth_vis", "--overwrite"]),
                ("p.visibility.los/p.visibility.los.py",
                 [f"dem={dem}",
                  f"scan_res={int(self._vis_sres.GetValue())}",
                  "prefix=los", "--overwrite"]),
            ]
        p5 = RunStepWxPage(self, session,
            "Step 5 — Visibility",
            "Earth visibility fraction and terrain horizon masking.",
            _build_vis_form, _build_vis_cmd)

        # ── Page 6: MCDM
        def _build_mcdm_form(panel, sizer):
            form = wx.FlexGridSizer(6, 2, 4, 8); form.AddGrowableCol(1,1)
            dw = session.get("weights",
                {"slope":0.25,"roughness":0.15,"illumination":0.20,
                 "earth_vis":0.15,"science":0.25})
            self._wsp = {}
            for k in ["slope","roughness","illumination","earth_vis","science"]:
                ctrl = wx.SpinCtrlDouble(panel, min=0, max=1,
                                          value=str(dw.get(k,0.2)), inc=0.05)
                self._wsp[k] = ctrl
                form.Add(wx.StaticText(panel, label=f"Weight {k}:"),
                         0, wx.ALIGN_CENTER_VERTICAL)
                form.Add(ctrl, 1, wx.EXPAND)
            self._mcdm_method = wx.Choice(panel,
                choices=["both","wlc","topsis"])
            self._mcdm_method.SetSelection(0)
            form.Add(wx.StaticText(panel, label="Method:"), 0, wx.ALIGN_CENTER_VERTICAL)
            form.Add(self._mcdm_method, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_mcdm_cmd():
            w = {k: v.GetValue() for k, v in self._wsp.items()}
            total = sum(w.values()) or 1
            w = {k: v/total for k, v in w.items()}
            session.set("weights", w)
            wstr = ",".join(str(w[k]) for k in
                ["slope","roughness","illumination","earth_vis","science"])
            method = self._mcdm_method.GetString(
                self._mcdm_method.GetSelection())
            return [("p.mcdm.score/p.mcdm.score.py",
                     ["slope=hazard_composite", "roughness=roughness_rms",
                      "illumination=illum_fraction",
                      "earth_vis=earth_vis_fraction",
                      "exclusion_masks=hazard_mask",
                      f"weights={wstr}", f"method={method}",
                      "prefix=suitability", "--overwrite"])]
        p6 = RunStepWxPage(self, session,
            "Step 6 — MCDM Scoring",
            "Weighted linear combination and TOPSIS suitability scoring.",
            _build_mcdm_form, _build_mcdm_cmd)

        # ── Page 7: Ranking
        def _build_rank_form(panel, sizer):
            form = wx.FlexGridSizer(4, 2, 4, 8); form.AddGrowableCol(1,1)
            self._min_area_wx = wx.SpinCtrlDouble(panel, min=0.001, max=1000,
                value=str(session.get("min_area",0.5)), inc=0.1)
            self._top_pct_wx  = wx.SpinCtrlDouble(panel, min=10, max=95,
                value=str(session.get("top_pct",50)))
            self._n_cand_wx   = wx.SpinCtrl(panel, min=1, max=50,
                value=str(session.get("n_cand",10)))
            self._mc_wx       = wx.SpinCtrl(panel, min=0, max=2000,
                value=str(session.get("mc_samples",200)))
            for lbl, ctrl in [
                ("Min area (km²):", self._min_area_wx),
                ("Top percentile (%):", self._top_pct_wx),
                ("Max candidates:", self._n_cand_wx),
                ("MC samples:", self._mc_wx)]:
                form.Add(wx.StaticText(panel, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
                form.Add(ctrl, 1, wx.EXPAND)
            sizer.Add(form, 0, wx.EXPAND | wx.ALL, 4)

        def _build_rank_cmd():
            rpt = str(Path.home() / "p_landing_report.json")
            session.set("report_path", rpt)
            session.set("min_area",   self._min_area_wx.GetValue())
            session.set("top_pct",    self._top_pct_wx.GetValue())
            session.set("n_cand",     self._n_cand_wx.GetValue())
            session.set("mc_samples", self._mc_wx.GetValue())
            return [("p.rank/p.rank.py",
                     ["suitability=suitability_wlc",
                      "criteria=hazard_composite,roughness_rms,"
                        "illum_fraction,earth_vis_fraction",
                      f"min_area_km2={self._min_area_wx.GetValue()}",
                      f"top_percentile={self._top_pct_wx.GetValue()}",
                      f"n_candidates={self._n_cand_wx.GetValue()}",
                      f"mc_samples={self._mc_wx.GetValue()}",
                      "prefix=rank", f"report={rpt}", "--overwrite"])]
        p7 = RunStepWxPage(self, session,
            "Step 7 — Ranking",
            "Extract and rank candidate sites with Monte Carlo sensitivity.",
            _build_rank_form, _build_rank_cmd)

        # Chain pages
        wx.adv.WizardPageSimple.Chain(p1, p2)
        wx.adv.WizardPageSimple.Chain(p2, p3)
        wx.adv.WizardPageSimple.Chain(p3, p4)
        wx.adv.WizardPageSimple.Chain(p4, p5)
        wx.adv.WizardPageSimple.Chain(p5, p6)
        wx.adv.WizardPageSimple.Chain(p6, p7)
        self._first_page = p1

    def run(self):
        result = self.RunWizard(self._first_page)
        if result:
            self.session.save()
            wx.MessageBox(
                f"Pipeline complete.\nReport: {self.session.get('report_path','')}",
                "Done", wx.OK | wx.ICON_INFORMATION)
        self.Destroy()

    def _pick_dem(self):
        dlg = wx.FileDialog(self, "Select DEM",
            wildcard="PDS label / GeoTIFF (*.lbl;*.LBL;*.tif;*.img)|*.lbl;*.LBL;*.tif;*.img")
        if dlg.ShowModal() == wx.ID_OK:
            self._dem_file.SetValue(dlg.GetPath())


# ── Session (re-use Qt version via JSON) ────────────────────────────────────

class _WxSession:
    def __init__(self):
        self.path = Path.home() / ".p_landing_wizard_session.json"
        self.data = {}
        try:
            with open(self.path) as f:
                self.data = json.load(f)
        except (OSError, json.JSONDecodeError):
            pass

    def get(self, k, d=None):  return self.data.get(k, d)
    def set(self, k, v):       self.data[k] = v
    def save(self):
        with open(self.path, "w") as f:
            json.dump(self.data, f, indent=2)


def run_wizard(parent=None):
    """Entry point called from g.gui.landing and from __main__."""
    if not WX_OK:
        print("ERROR: wxPython is not installed.", file=sys.stderr)
        return
    app = wx.GetApp() or wx.App(False)
    session = _WxSession()
    wiz = LandingWxWizard(parent or wx.Frame(None), session)
    wiz.run()


if __name__ == "__main__":
    run_wizard()
