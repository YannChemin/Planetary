#!/usr/bin/env python3
############################################################################
# MODULE:       p.landing
# PURPOSE:      Master end-to-end pipeline for planetary landing-site
#               evaluation.  Chains all p.* modules in order, reading a JSON
#               state file so already-completed stages can be skipped.
#               Cleans up all module-prefixed temporary maps on completion.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: End-to-end planetary landing-site evaluation pipeline.
# % keyword: Planetary
# % keyword: Landing Pipeline
# % keyword: pipeline
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres, already in GRASS mapset)
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: body
# % label: Body descriptor JSON file
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: mission
# % label: Mission configuration JSON file
# % required: yes
# %end

# %option
# % key: stages
# % type: string
# % label: Comma-separated stages to run
# % description: terrain,illumination,visibility,mcdm,rank
# % answer: terrain,illumination,visibility,mcdm,rank
# % required: no
# %end

# %option
# % key: skip
# % type: string
# % label: Comma-separated stages to skip
# % required: no
# %end

# %option
# % key: ancillary
# % type: string
# % label: JSON mapping: ancillary layer name → GRASS raster map name
# % description: e.g. '{"slope":"slope_30m","roughness":"roughness_rms","illum":"illum_fraction","earth_vis":"earth_vis_fraction"}'
# % required: no
# %end

# %option
# % key: state
# % type: string
# % label: JSON state file to track completed stages
# % answer: .p_landing_state.json
# % required: no
# %end

# %option G_OPT_F_OUTPUT
# % key: report
# % label: Output JSON summary report filename
# % answer: landing_report.json
# % required: no
# %end

# %option
# % key: nprocs
# % type: integer
# % label: OpenMP threads forwarded to terrain (r.slope.aspect, r.neighbors) and visibility (r.horizon)
# % description: Default 4 (minimum recommended). Mission JSON 'nprocs' overrides; explicit values pass through verbatim.
# % answer: 4
# % required: no
# %end

# %option
# % key: memory
# % type: integer
# % label: Row-cache size in MB forwarded to r.slope.aspect / r.neighbors / r.horizon
# % description: Default 3000 (minimum recommended). Mission JSON 'memory' overrides; explicit values pass through verbatim.
# % answer: 3000
# % required: no
# %end

# %flag
# % key: c
# % description: Clean up all temporary maps (prefix pterr_,pillum_,etc.) when done
# %end

# %flag
# % key: f
# % description: Force re-run of all stages even if state file says they are done
# %end

import os
import sys
import json
import time
import textwrap
import subprocess

import grass.script as gs
from grass.exceptions import CalledModuleError

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import body_params, mission_params

ALL_STAGES = ["terrain", "illumination", "visibility", "mcdm", "rank"]

TMP_PREFIXES = [
    "pterr_", "pillum_", "pvis_", "psci_", "pmcdm_", "prank_", "pin_",
]

# ── pretty terminal output helpers ───────────────────────────────────────────

WIDTH      = 72
HRULE      = "─" * WIDTH
DRULE      = "═" * WIDTH
CHECK_OK   = "✓"
CHECK_FAIL = "✗"
CHECK_SKIP = "↷"


def _msg(line=""):
    """Plain line to stderr (no GRASS prefix)."""
    # g.message rejects an empty message, so emit blank/whitespace lines
    # straight to stderr (where g.message writes too).
    if line.strip():
        gs.message(line)
    else:
        sys.stderr.write(line + "\n")


def _banner(title, version=None):
    """Top banner with double rules."""
    line = title if not version else f"{title}  —  {version}"
    pad  = max(0, (WIDTH - len(line)) // 2)
    _msg(DRULE)
    _msg(" " * pad + line)
    _msg(DRULE)


def _kv(label, value, label_width=18):
    """Print 'Label   value' indented two spaces, wrapping long values."""
    indent = "  " + label.ljust(label_width)
    cont   = "  " + " " * label_width
    text   = str(value)
    wrapped = textwrap.wrap(text, width=WIDTH - len(indent),
                            break_long_words=False, break_on_hyphens=False)
    if not wrapped:
        _msg(indent)
        return
    _msg(indent + wrapped[0])
    for line in wrapped[1:]:
        _msg(cont + line)


def _section(title):
    """Light horizontal rule with a left-aligned title."""
    _msg("")
    _msg(HRULE)
    _msg(f"  {title}")
    _msg(HRULE)


def _stage_header(idx, total, name):
    """Stage frame: '[n/N] name'."""
    _msg("")
    _msg(HRULE)
    _msg(f"  [{idx}/{total}] {name}")
    _msg(HRULE)


def _stage_footer(name, status, duration_s):
    """End-of-stage line with status mark and HH:MM:SS."""
    mark = {"ok": CHECK_OK, "fail": CHECK_FAIL, "skip": CHECK_SKIP}.get(status, "•")
    _msg(f"  {mark} {name} ({_fmt_duration(duration_s)})")


def _fmt_duration(seconds):
    """Format seconds as H:MM:SS or M:SS."""
    seconds = int(round(seconds))
    h, rem = divmod(seconds, 3600)
    m, s   = divmod(rem, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def _format_region(reg):
    """Compact region description, e.g. '30 m × 5056 × 5056 (25 563 136 cells)'."""
    nsr = reg["nsres"]
    ewr = reg["ewres"]
    rows = int(reg["rows"])
    cols = int(reg["cols"])
    cells = rows * cols
    return (f"{nsr:g} × {ewr:g} m, {rows} × {cols} cells "
            f"({cells:,} total)")


def _format_extent(reg):
    """Bounding box one-liner."""
    return (f"n={reg['n']:.0f}  s={reg['s']:.0f}  "
            f"e={reg['e']:.0f}  w={reg['w']:.0f}")


def _candidate_table(report_path):
    """Render the rank candidate list (if present) as an ASCII box."""
    if not os.path.isfile(report_path):
        return
    try:
        with open(report_path) as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return
    cands = data.get("candidates") or []
    if not cands:
        # Surface the largest-candidate diagnostic if p.rank found none
        lc = data.get("largest_candidate")
        if lc:
            _msg("")
            _msg("  No candidates met the area + percentile threshold.")
            _msg(f"  Largest contiguous region: {lc.get('area_km2', 0):.1f} km² "
                 f"({lc.get('n_clumps', '?')} clumps total).")
        return
    _msg("")
    _msg("  Top candidates")
    _msg("  ┌──────┬──────────────────┬─────────────┬──────────────┐")
    _msg("  │ Rank │ Mean suitability │  Area (km²) │ Rank1 prob.  │")
    _msg("  ├──────┼──────────────────┼─────────────┼──────────────┤")
    for c in cands:
        rank = c.get("rank", "?")
        mean = c.get("suit_mean", 0)
        area = c.get("area_km2", 0)
        prob = c.get("rank1_probability")
        prob_s = f"{prob:.3f}" if isinstance(prob, (int, float)) else "—"
        _msg(f"  │ #{rank:<3} │     {mean:>8.4f}     │ {area:>10.1f}  │  {prob_s:>10}  │")
    _msg("  └──────┴──────────────────┴─────────────┴──────────────┘")


def load_state(state_file):
    if os.path.isfile(state_file):
        with open(state_file) as f:
            return json.load(f)
    return {}


def save_state(state_file, state):
    with open(state_file, "w") as f:
        json.dump(state, f, indent=2)


def run_module(module, **kwargs):
    """Run a GRASS module via grass.script, passing keyword args as params."""
    flags_str = ""
    params = {}
    for k, v in kwargs.items():
        if k == "_flags":
            flags_str = v
        else:
            params[k] = v
    gs.run_command(module, flags=flags_str, quiet=False, **params)


def run_stage_terrain(dem, mission, anc, overwrite):
    """Run p.terrain.slope, p.terrain.roughness, p.terrain.hazard."""
    mc = mission
    _PAR = {"nprocs": mc.get("_nprocs", 4), "memory": mc.get("_memory", 3000)}

    # Slope thresholds from mission config
    thr_dict = mc.get("slope_thresholds_deg", {"30": 15, "100": 10, "1000": 7})
    scales     = ",".join(str(s) for s in thr_dict.keys())
    thresholds = ",".join(str(v) for v in thr_dict.values())

    _msg("  ▸ p.terrain.slope")
    slope_map = anc.get("slope", "")
    if not slope_map:
        gs.run_command(
            "p.terrain.slope",
            dem=dem,
            scales=scales,
            thresholds=thresholds,
            prefix="slope",
            overwrite=overwrite, **_PAR,
        )
        slope_map = f"slope_{list(thr_dict.keys())[0]}m"

    _msg("  ▸ p.terrain.roughness")
    rough_map = anc.get("roughness", "")
    if not rough_map:
        gs.run_command(
            "p.terrain.roughness",
            dem=dem,
            prefix="roughness",
            overwrite=overwrite, **_PAR,
        )
        rough_map = "roughness_rms"

    _msg("  ▸ p.terrain.hazard")
    slope_max = mc.get("hard_exclusion", {}).get("slope_max_deg", 15)
    rough_max = mc.get("hard_exclusion", {}).get("roughness_rms_max_m", 1.0)
    gs.run_command(
        "p.terrain.hazard",
        dem=dem,
        slope=slope_map,
        roughness=rough_map,
        slope_max=slope_max,
        roughness_max=rough_max,
        prefix="hazard",
        overwrite=overwrite, **_PAR,
    )

    return {
        "slope":       slope_map,
        "roughness":   rough_map,
        "hazard":      "hazard_composite",
        "hazard_mask": "hazard_mask",
    }


def run_stage_illumination(dem, body_file, mission, anc, overwrite):
    """Run p.illumination.sunfraction."""
    mc = mission
    nsteps   = mc.get("illum_nsteps", 36)
    scan_res = float(mc.get("scan_res", 0) or 0)
    ephem    = mc.get("ephemeris", "auto")
    start_ep = mc.get("start_epoch", "")

    illum_map = anc.get("illum", anc.get("illumination", ""))
    if illum_map:
        gs.message(f"Using pre-computed illumination: {illum_map}")
        return {"illum_fraction": illum_map}

    _msg(f"  ▸ p.illumination.sunfraction  (nsteps={nsteps}, ephemeris={ephem}"
         + (f", scan_res={int(scan_res)} m)" if scan_res > 0 else ")"))
    kwargs = dict(
        dem=dem,
        body=body_file,
        nsteps=nsteps,
        sunmask_module="p.sunmask",
        prefix="illum",
        ephemeris=ephem,
        overwrite=overwrite,
    )
    if scan_res > 0:
        kwargs["scan_res"] = scan_res
    if start_ep:
        kwargs["start_epoch"] = start_ep
    win = float(mc.get("mission_window_days", 0) or 0)
    if win > 0:
        kwargs["window_days"] = win
    gs.run_command("p.illumination.sunfraction", **kwargs)
    return {"illum_fraction": "illum_fraction"}


def run_stage_visibility(dem, body_file, mission, anc, overwrite):
    """Run p.visibility.earth."""
    mc = mission
    nsteps   = mc.get("vis_nsteps", 36)
    min_el   = mc.get("earth_elevation_min_deg", 3.0)
    scan_res = float(mc.get("scan_res", 0) or 0)
    ephem    = mc.get("ephemeris", "auto")
    start_ep = mc.get("start_epoch", "")

    evis_map = anc.get("earth_vis", "")
    if evis_map:
        gs.message(f"Using pre-computed earth visibility: {evis_map}")
        return {"earth_vis_fraction": evis_map}

    _msg(f"  ▸ p.visibility.earth  (nsteps={nsteps}, min_elev={min_el}°, ephemeris={ephem}"
         + (f", scan_res={int(scan_res)} m)" if scan_res > 0 else ")"))
    kwargs = dict(
        dem=dem,
        body=body_file,
        nsteps=nsteps,
        min_elevation=min_el,
        prefix="earth_vis",
        ephemeris=ephem,
        nprocs=mc.get("_nprocs", 4),
        memory=mc.get("_memory", 3000),
        overwrite=overwrite,
    )
    if scan_res > 0:
        kwargs["scan_res"] = scan_res
    if start_ep:
        kwargs["start_epoch"] = start_ep
    win = float(mc.get("mission_window_days", 0) or 0)
    if win > 0:
        kwargs["window_days"] = win
    gs.run_command("p.visibility.earth", **kwargs)

    out = {"earth_vis_fraction": "earth_vis_fraction"}

    # Optional orbital-relay visibility (e.g. Gateway/NRHO for Artemis).
    # Triggered by either an explicit orbiter_altitude_km in the mission JSON
    # or a non-zero orbiter_vis weight in criteria_weights.
    weights = mc.get("criteria_weights", {}) or {}
    if mc.get("orbiter_altitude_km") or float(weights.get("orbiter_vis", 0) or 0) > 0:
        alt = float(mc.get("orbiter_altitude_km", 100))
        inc = float(mc.get("orbiter_inclination_deg", 90))
        n_orb = int(mc.get("orbiter_norbits", 14))
        steps = int(mc.get("orbiter_steps_per_orbit", 72))
        min_el_orb = float(mc.get("orbiter_min_elev_deg", 5.0))
        _msg(f"  ▸ p.visibility.orbiter  (alt={alt:.0f} km, incl={inc:.0f}°, "
             f"{n_orb} orbits × {steps} steps)")
        gs.run_command("p.visibility.orbiter",
                       dem=dem, body=body_file,
                       altitude_km=alt, inclination=inc,
                       norbits=n_orb, steps_per_orbit=steps,
                       min_elev_deg=min_el_orb,
                       nprocs=mc.get("_nprocs", 4),
                       memory=mc.get("_memory", 3000),
                       prefix="orbiter", overwrite=overwrite)
        out["orbiter_contact_fraction"] = "orbiter_contact_fraction"

    return out


def run_stage_mcdm(terrain_out, illum_out, vis_out, mission, anc, overwrite):
    """Run p.mcdm.score."""
    mc = mission
    weights = mc.get("criteria_weights", {})
    w = (
        f"{weights.get('slope', 0.25)},"
        f"{weights.get('roughness', 0.15)},"
        f"{weights.get('illumination', 0.20)},"
        f"{weights.get('earth_vis', 0.15)},"
        f"{weights.get('orbiter_vis', 0.0)},"
        f"{weights.get('science', 0.25)}"
    )

    excl_maps = [
        m for m in [terrain_out.get("hazard_mask"), ]
        if m and _map_exists(m)
    ]

    _msg("  ▸ p.mcdm.score  (method=wlc)")
    kwargs = dict(
        weights=w,
        method="wlc",
        prefix="suitability",
        overwrite=overwrite,
    )
    # Use raw slope (not the hazard composite) so slope is not double-counted
    # against the other criteria. The hazard composite is used only for exclusion.
    if terrain_out.get("slope") and _map_exists(terrain_out["slope"]):
        kwargs["slope"] = terrain_out["slope"]
    elif terrain_out.get("hazard"):
        kwargs["slope"] = terrain_out["hazard"]
    if terrain_out.get("roughness") and _map_exists(terrain_out["roughness"]):
        kwargs["roughness"] = terrain_out["roughness"]
    if illum_out.get("illum_fraction") and _map_exists(illum_out["illum_fraction"]):
        kwargs["illumination"] = illum_out["illum_fraction"]
    if vis_out.get("earth_vis_fraction") and _map_exists(vis_out["earth_vis_fraction"]):
        kwargs["earth_vis"] = vis_out["earth_vis_fraction"]
    if vis_out.get("orbiter_contact_fraction") and _map_exists(vis_out["orbiter_contact_fraction"]):
        kwargs["orbiter_vis"] = vis_out["orbiter_contact_fraction"]
    if excl_maps:
        kwargs["exclusion_masks"] = ",".join(excl_maps)
    if anc.get("science") and _map_exists(anc["science"]):
        kwargs["science"] = anc["science"]

    gs.run_command("p.mcdm.score", **kwargs)
    return {"suitability": "suitability_wlc"}


def run_stage_rank(suit_out, mission, overwrite, report,
                   terrain_out=None, illum_out=None, vis_out=None, anc=None):
    """Run p.rank."""
    mc = mission
    suit_map = suit_out.get("suitability", "suitability_wlc")
    if not _map_exists(suit_map):
        gs.fatal(f"Suitability map '{suit_map}' not found. Run mcdm stage first.")

    # min_area_km2 from the mission JSON wins when set; otherwise fall back
    # to the ellipse-area heuristic (one lander footprint), which is the
    # right default for Luna-27-scale 30 km regions but far too aggressive
    # for tight polar 5 m sweeps where suitability naturally fragments
    # into 1–10 km² patches (Artemis 15 × 12.5 km ellipse → 147 km²).
    if "min_area_km2" in mc and mc["min_area_km2"] is not None:
        min_area = float(mc["min_area_km2"])
    else:
        min_area = (mc.get("ellipse_major_m", 30000) *
                    mc.get("ellipse_minor_m", 15000) * 3.14159 / 4 / 1e6)
    top_pct = mc.get("top_percentile", 70.0)

    # Forward the criterion maps when available so p.rank's Monte-Carlo
    # block can perturb weights properly (otherwise it falls back to
    # mean±std perturbation of the suitability score itself).
    crit = []
    for d, k in ((terrain_out or {}, "slope"),
                 (terrain_out or {}, "roughness"),
                 (illum_out or {}, "illum_fraction"),
                 (vis_out or {}, "earth_vis_fraction"),
                 (vis_out or {}, "orbiter_contact_fraction"),
                 (anc or {}, "science")):
        m = d.get(k)
        if m and _map_exists(m):
            crit.append(m)

    _msg(f"  ▸ p.rank  (top_percentile={top_pct}, min_area={min_area:.1f} km²)")
    kwargs = dict(
        suitability=suit_map,
        min_area_km2=min_area,
        top_percentile=top_pct,
        n_candidates=10,
        mc_samples=200,
        prefix="rank",
        report=report,
        overwrite=overwrite,
    )
    if crit:
        kwargs["criteria"] = ",".join(crit)
    gs.run_command("p.rank", **kwargs)
    return {"candidates": "rank_candidates", "uncertainty": "rank_uncertainty"}


def _map_exists(mapname):
    maps = gs.list_grouped("raster").get(gs.gisenv()["MAPSET"], [])
    return mapname in maps


def _cleanup_tmp():
    mapset = gs.gisenv()["MAPSET"]
    all_maps = gs.list_grouped("raster").get(mapset, [])
    to_del = []
    for m in all_maps:
        for pfx in TMP_PREFIXES:
            if m.startswith(pfx):
                to_del.append(m)
                break
    if to_del:
        gs.message(f"Removing {len(to_del)} temporary raster maps…")
        gs.run_command("g.remove", type="raster",
                       name=",".join(to_del), flags="f", quiet=True)


def main():
    opt_dem     = options["dem"]
    opt_body    = options["body"]
    opt_mission = options["mission"]
    opt_stages  = options["stages"]
    opt_skip    = options["skip"]
    opt_anc_raw = options["ancillary"]
    opt_state   = options["state"]
    opt_report  = options["report"]
    opt_nprocs  = int(options.get("nprocs", 4) or 4)
    opt_memory  = int(options.get("memory", 3000) or 3000)
    flag_clean  = flags["c"]
    flag_force  = flags["f"]

    overwrite = gs.overwrite()

    # If -f (force re-run) flag is used, automatically enable overwrite
    if flag_force and not overwrite:
        overwrite = True

    body    = body_params(opt_body)
    mission = mission_params(opt_mission)
    anc     = json.loads(opt_anc_raw) if opt_anc_raw else {}

    # Mission JSON may pin nprocs/memory for reproducibility; explicit values
    # win over the CLI default. Pass through verbatim (no clamping) so a
    # _fast.json preview can deliberately undercut the default minimum.
    nprocs = int(mission.get("nprocs", opt_nprocs))
    memory = int(mission.get("memory", opt_memory))
    mission["_nprocs"] = nprocs
    mission["_memory"] = memory

    # ── set computational region ──────────────────────────────────────────
    # Priority:
    #   1. mission["region_bounds"]   → explicit study-area bounds (+optional res)
    #   2. mission["scan_res"]        → align to DEM extent at scan_res
    #   3. else                        → align to DEM extent at native posting
    # Rationale: on high-resolution DEMs (HiRISE 1 m, NAC 2 m) the native
    # posting can push the cell count past 100 M, which makes the terrain
    # stage dominate wall time for no scientific benefit when the rest of
    # the pipeline (illum/vis) is already downsampling internally to
    # scan_res. Honouring scan_res as the main-region resolution gives the
    # user a single knob that affects the whole pipeline consistently.
    rb        = mission.get("region_bounds")
    scan_res  = float(mission.get("scan_res", 0) or 0)

    if rb:
        # Explicit study-area bounds. Resolution falls back to scan_res when
        # the bounds dict has no res/nsres/ewres key.
        g_region_kw = {}
        for k in ("n", "s", "e", "w", "res", "nsres", "ewres"):
            if k in rb:
                g_region_kw[k] = rb[k]
        if scan_res > 0 and not any(k in g_region_kw
                                    for k in ("res", "nsres", "ewres")):
            g_region_kw["res"] = scan_res
        try:
            gs.run_command("g.region", flags="a", quiet=True, **g_region_kw)
        except Exception as e:
            gs.warning(f"Could not apply region_bounds: {e}")

        # ── intersect region with the DEM's data extent ───────────────────
        # If region_bounds extends beyond the DEM, all downstream stats end
        # up nan/-inf and r.mapcalc dies. Clip the active region to the DEM.
        try:
            dem_info = gs.raster_info(opt_dem)
            r        = gs.region()
            new_n = min(r["n"], dem_info["north"])
            new_s = max(r["s"], dem_info["south"])
            new_e = min(r["e"], dem_info["east"])
            new_w = max(r["w"], dem_info["west"])
            if (new_n != r["n"] or new_s != r["s"]
                    or new_e != r["e"] or new_w != r["w"]):
                if new_n <= new_s or new_e <= new_w:
                    gs.fatal(
                        f"region_bounds does not intersect the DEM "
                        f"'{opt_dem}' extent. DEM covers "
                        f"n={dem_info['north']:.1f} s={dem_info['south']:.1f} "
                        f"e={dem_info['east']:.1f} w={dem_info['west']:.1f}. "
                        f"Adjust mission.region_bounds or use a wider DEM."
                    )
                gs.warning(
                    f"region_bounds extends beyond the DEM '{opt_dem}'; "
                    f"clipping region to DEM extent "
                    f"(n={new_n:.1f} s={new_s:.1f} e={new_e:.1f} w={new_w:.1f})."
                )
                gs.run_command("g.region", flags="a", quiet=True,
                               n=new_n, s=new_s, e=new_e, w=new_w)
        except CalledModuleError as e:
            gs.warning(f"Could not read DEM extent for clipping: {e}")
    else:
        # No explicit bounds: align to the DEM, optionally downsampling to
        # scan_res. Without this, a freshly-aligned native-posting region
        # (e.g. the one p.in.archive 0.8.6 sets at import time) makes
        # the terrain stage process 100 M+ cells on HiRISE-class DEMs.
        try:
            if scan_res > 0:
                gs.message(
                    f"No region_bounds in mission JSON; aligning region to "
                    f"DEM '{opt_dem}' at scan_res={scan_res:g} m "
                    f"(saves the terrain stage from running at native "
                    f"posting on high-resolution DEMs).")
                gs.run_command("g.region", raster=opt_dem, res=scan_res,
                               flags="a", quiet=True)
            else:
                gs.run_command("g.region", raster=opt_dem, quiet=True)
        except CalledModuleError as e:
            gs.warning(f"Could not align region to DEM '{opt_dem}': {e}")

    stages_req  = set(s.strip() for s in opt_stages.split(",") if s.strip())
    stages_skip = set(s.strip() for s in opt_skip.split(",") if s.strip())
    stages_run  = [s for s in ALL_STAGES
                   if s in stages_req and s not in stages_skip]

    state = load_state(opt_state)
    if flag_force:
        # Only invalidate the stages about to re-run. Preserve output dicts
        # (terrain_out / illum_out / vis_out / suit_out) so a partial rerun
        # like `stages=visibility,mcdm -f` can still feed earlier outputs
        # into downstream stages.
        for _name in stages_run:
            state[_name] = False

    # ── banner + run setup ────────────────────────────────────────────────
    pkg_ver = ""
    try:
        with open("/usr/local/share/p-landing-grass/VERSION") as f:
            pkg_ver = "v" + f.read().strip()
    except OSError:
        pass

    _banner("p.landing", pkg_ver or None)
    _msg("")
    _kv("Body / Mission",
        f"{body.get('name','?')} / {mission.get('mission','?')}")
    _kv("DEM", opt_dem)
    reg = gs.region()
    _kv("Region",  _format_region(reg))
    _kv("Extent",  _format_extent(reg))
    _kv("Stages",  " → ".join(stages_run))
    _kv("Report",  opt_report)
    _kv("State",   opt_state + (" (force re-run)" if flag_force else ""))

    sa_note = mission.get("study_area_note")
    if sa_note:
        _msg("")
        _kv("Study-area note", sa_note)

    terrain_out = {}
    illum_out   = {}
    vis_out     = {}
    suit_out    = {}

    # Restore previous outputs from state if skipping stages
    terrain_out = state.get("terrain_out", {})
    illum_out   = state.get("illum_out", {})
    vis_out     = state.get("vis_out", {})
    suit_out    = state.get("suit_out", {})

    stage_status = {}
    pipeline_t0 = time.monotonic()
    stage_idx_counter = {"i": 0}
    n_total_stages = len(stages_run)

    def _run_stage(name, fn, *args, **kwargs):
        """Run a pipeline stage, catching all errors and recording status."""
        stage_idx_counter["i"] += 1
        idx = stage_idx_counter["i"]
        _stage_header(idx, n_total_stages, name)
        t0 = time.monotonic()

        if state.get(name) and not flag_force:
            _msg(f"  skipping — already done per state file ({opt_state})")
            stage_status[name] = {
                "ok": True, "skipped": True, "duration_s": 0.0,
            }
            _stage_footer(name, "skip", 0.0)
            return None

        try:
            out = fn(*args, **kwargs)
            dt = time.monotonic() - t0
            state[name] = True
            stage_status[name] = {
                "ok": True, "skipped": False, "duration_s": round(dt, 2),
            }
            save_state(opt_state, state)
            _stage_footer(name, "ok", dt)
            return out
        except CalledModuleError as e:
            dt = time.monotonic() - t0
            err_text = getattr(e, "errors", None) or str(e)
            stage_status[name] = {
                "ok": False,
                "error_type": "CalledModuleError",
                "error": str(e),
                "stderr": err_text,
                "duration_s": round(dt, 2),
            }
            _msg("")
            if "exists" in str(e).lower():
                gs.error(
                    f"[stage: {name}] Output maps already exist in this mapset. "
                    "Use --overwrite or -f flag to re-run."
                )
            else:
                gs.error(f"[stage: {name}] FAILED: {e}")
                if err_text and err_text != str(e):
                    gs.error(f"  module stderr: {err_text[:500]}")
            _stage_footer(name, "fail", dt)
            return None
        except Exception as e:
            dt = time.monotonic() - t0
            stage_status[name] = {
                "ok": False,
                "error_type": type(e).__name__,
                "error": str(e),
                "duration_s": round(dt, 2),
            }
            _msg("")
            gs.error(f"[stage: {name}] FAILED ({type(e).__name__}): {e}")
            _stage_footer(name, "fail", dt)
            return None

    def _failed_upstream(*names):
        return any(stage_status.get(n, {}).get("ok") is False for n in names)

    if "terrain" in stages_run:
        out = _run_stage("terrain", run_stage_terrain,
                         opt_dem, mission, anc, overwrite)
        if out is not None:
            terrain_out = out
            state["terrain_out"] = terrain_out

    if "illumination" in stages_run:
        out = _run_stage("illumination", run_stage_illumination,
                         opt_dem, opt_body, mission, anc, overwrite)
        if out is not None:
            illum_out = out
            state["illum_out"] = illum_out

    if "visibility" in stages_run:
        out = _run_stage("visibility", run_stage_visibility,
                         opt_dem, opt_body, mission, anc, overwrite)
        if out is not None:
            vis_out = out
            state["vis_out"] = vis_out

    if "mcdm" in stages_run:
        if _failed_upstream("terrain", "illumination", "visibility"):
            stage_idx_counter["i"] += 1
            _stage_header(stage_idx_counter["i"], n_total_stages, "mcdm")
            _msg("  ✗ skipped — upstream stage failed")
            stage_status["mcdm"] = {
                "ok": False, "error": "upstream stage failed",
                "duration_s": 0.0,
            }
        else:
            out = _run_stage("mcdm", run_stage_mcdm,
                             terrain_out, illum_out, vis_out,
                             mission, anc, overwrite)
            if out is not None:
                suit_out = out
                state["suit_out"] = suit_out

    if "rank" in stages_run:
        if _failed_upstream("mcdm"):
            stage_idx_counter["i"] += 1
            _stage_header(stage_idx_counter["i"], n_total_stages, "rank")
            _msg("  ✗ skipped — mcdm stage failed")
            stage_status["rank"] = {
                "ok": False, "error": "upstream stage failed",
                "duration_s": 0.0,
            }
        else:
            out = _run_stage("rank", run_stage_rank,
                             suit_out, mission, overwrite, opt_report,
                             terrain_out=terrain_out, illum_out=illum_out,
                             vis_out=vis_out, anc=anc)
            if out is not None:
                state["rank_out"] = out

    # ── final summary report ───────────────────────────────────────────────
    total_dt = time.monotonic() - pipeline_t0
    n_failed = sum(1 for s in stage_status.values() if not s.get("ok"))
    n_ok     = sum(1 for s in stage_status.values()
                   if s.get("ok") and not s.get("skipped"))
    n_skip   = sum(1 for s in stage_status.values() if s.get("skipped"))
    summary = {
        "body":     body.get("name"),
        "mission":  mission.get("mission"),
        "dem":      opt_dem,
        "stages_run": stages_run,
        "stage_status": stage_status,
        "status":   "ok" if n_failed == 0 else f"completed_with_errors ({n_failed} stage(s) failed)",
        "duration_s": round(total_dt, 2),
        "study_area_note":      mission.get("study_area_note"),
        "terrain_outputs":      terrain_out,
        "illumination_outputs": illum_out,
        "visibility_outputs":   vis_out,
        "suitability_outputs":  suit_out,
    }
    # p.rank writes its own report at opt_report during the rank stage; merge
    # its candidates/thresholds/etc. into the summary so p.rank.cross can
    # ingest the final file (otherwise this dump would wipe them).
    if os.path.isfile(opt_report):
        try:
            with open(opt_report) as f:
                rank_report = json.load(f)
            for k in ("candidates", "threshold", "top_percentile",
                      "min_area_km2", "n_candidates", "uncertainty",
                      "monte_carlo"):
                if k in rank_report and k not in summary:
                    summary[k] = rank_report[k]
        except (OSError, ValueError):
            pass
    with open(opt_report, "w") as f:
        json.dump(summary, f, indent=2)

    # ── pretty summary table + candidate list ─────────────────────────────
    _section("Summary")
    _msg("")
    for s in stages_run:
        st  = stage_status.get(s, {})
        if st.get("skipped"):
            mark, lbl = CHECK_SKIP, "skipped"
        elif st.get("ok"):
            mark, lbl = CHECK_OK, "ok"
        else:
            mark, lbl = CHECK_FAIL, "failed"
        dt  = st.get("duration_s", 0.0)
        _msg(f"  {mark}  {s:<14} {lbl:<8}  {_fmt_duration(dt)}")
    _msg("")
    _kv("Totals",
        f"{n_ok} ok · {n_skip} skipped · {n_failed} failed  "
        f"({_fmt_duration(total_dt)} total)")
    _kv("Report", opt_report)

    # Pretty candidate table if rank produced a report
    if "rank" in stages_run and stage_status.get("rank", {}).get("ok"):
        _candidate_table(opt_report)

    _msg("")
    if n_failed == 0:
        _banner(f"Pipeline complete  {CHECK_OK}")
    else:
        _banner(f"Pipeline finished with {n_failed} failure(s)  {CHECK_FAIL}")
        gs.warning(f"See {opt_report} for per-stage error details.")

    if flag_clean:
        _msg("")
        _msg("  Cleaning up temporary maps…")
        _cleanup_tmp()


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
