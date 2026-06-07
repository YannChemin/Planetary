#!/usr/bin/env python3
############################################################################
# MODULE:       p.illumination.sunfraction
# PURPOSE:      Time-averaged solar illumination fraction and permanently
#               shadowed region (PSR) mask over a complete nutation/orbital
#               cycle of any planetary body.
#               Uses r.sunmask (with explicit azimuth/elevation) and
#               r.series; does not duplicate either module.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Time-averaged solar illumination fraction and PSR mask for any planetary body.
# % keyword: Planetary
# % keyword: Illumination
# % keyword: shadow
# % keyword: solar
# % keyword: landing
# %end

# %option G_OPT_R_INPUT
# % key: dem
# % label: Input DEM raster (metres)
# % required: yes
# %end

# %option G_OPT_F_INPUT
# % key: body
# % label: Body descriptor JSON file
# % required: yes
# %end

# %option
# % key: nsteps
# % type: integer
# % label: Number of time steps to simulate
# % description: Divides nutation cycle (or `window_days` when set): 36 for quick tests, 360 for production.
# % answer: 36
# % required: no
# %end

# %option
# % key: window_days
# % type: double
# % label: Sample only over a window of this many days starting at start_epoch
# % description: 0 (default) means sample across the body's full nutation cycle, the conventional long-term illumination integration. Set to a positive value to model a short mission window (e.g. 6.5 for an Artemis-style 6.5-day surface stay); the `nsteps` samples are then spread evenly across that window only.
# % answer: 0
# % required: no
# %end

# %option
# % key: min_elevation
# % type: double
# % label: Minimum solar elevation angle to count as illuminated (degrees)
# % answer: 0.0
# % required: no
# %end

# %option
# % key: prefix
# % type: string
# % label: Output map name prefix
# % answer: illum
# % required: no
# %end

# %option
# % key: sunmask_module
# % type: string
# % label: Shadow-mask module to call (r.sunmask or p.sunmask)
# % description: Use p.sunmask for OpenMP+OpenCL acceleration (recommended).
# % answer: p.sunmask
# % required: no
# %end

# %option
# % key: scan_res
# % type: double
# % label: Resolution for shadow computation in metres (0 = native DEM resolution)
# % description: Coarsen to e.g. 30 or 100 m to speed up per-step shadow maps and match published methodology resolutions.
# % answer: 0
# % required: no
# %end

# %option
# % key: ephemeris
# % type: string
# % label: Sub-solar point model
# % description: auto = SPICE if configured, else Meeus (Moon), else analytic; spice = force SPICE (needs p.spice.config); meeus = force Meeus (Moon only); analytic = force toy model.
# % options: auto,spice,meeus,analytic
# % answer: auto
# % required: no
# %end

# %option
# % key: start_epoch
# % type: string
# % label: UTC start epoch for the real ephemeris (ISO-8601, e.g. 2028-01-01T00:00:00)
# % description: Only used when ephemeris=auto/meeus. Defaults to body JSON 'start_epoch' or J2000.
# % required: no
# %end

# %flag
# % key: k
# % description: Keep all per-timestep mask maps (warning: many maps)
# %end

# %flag
# % key: c
# % description: Force the chunked subprocess path (disable in-RAM ctypes fast path)
# %end

import os
import sys
import math
import atexit

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from p_lib import (body_params, cleanup_prefix, sun_position_moon, sun_position_generic,
                   sun_position_meeus,
                   region_center_geographic, solar_elevation_azimuth,
                   subsolar_point_moon, iso_to_jd)
import p_spice

_PREFIX_TMP = "pillum_sun_"


# ── in-RAM fast path (libpsunmask + numpy) ───────────────────────────────────
#
# Streams the same algorithm as the chunked path but keeps the DEM, the per-
# step shadow mask, and the running accumulators all in RAM, calling the
# shared library libpsunmask.so once per step via ctypes. Eliminates the
# fork + GRASS raster I/O cost that dominates 1000-step polar runs.

try:
    import ctypes
    import numpy as _np
    from grass.script import array as _garray
    _HAVE_INRAM_DEPS = True
except ImportError:
    _HAVE_INRAM_DEPS = False


def _libpsunmask_load():
    """Locate and bind libpsunmask.so; return a dict of bound functions
    {'cast', 'gpu_open', 'gpu_cast', 'gpu_close'} or None. GPU symbols may be
    bound but inert on builds without OpenCL (gpu_open returns NULL)."""
    import ctypes.util
    candidates = [
        os.environ.get("LIBPSUNMASK"),
        "/usr/local/lib/libpsunmask.so",
        "/usr/lib/libpsunmask.so",
        ctypes.util.find_library("psunmask"),
    ]
    for path in candidates:
        if not path:
            continue
        try:
            lib = ctypes.CDLL(path)
            cast = lib.psunmask_cast
        except (OSError, AttributeError):
            continue
        cast.restype = None
        cast.argtypes = [
            ctypes.POINTER(ctypes.c_float),
            ctypes.c_int, ctypes.c_int,
            ctypes.c_double, ctypes.c_double,
            ctypes.c_double, ctypes.c_double,
            ctypes.c_float,
            ctypes.POINTER(ctypes.c_ubyte),
        ]
        api = {"cast": cast, "gpu_open": None, "gpu_cast": None, "gpu_close": None}
        try:
            gopen = lib.psunmask_gpu_open
            gopen.restype = ctypes.c_void_p
            gopen.argtypes = [
                ctypes.POINTER(ctypes.c_float),
                ctypes.c_int, ctypes.c_int,
                ctypes.c_double, ctypes.c_double,
                ctypes.c_float,
                ctypes.c_char_p, ctypes.c_size_t,
            ]
            gcast = lib.psunmask_gpu_cast
            gcast.restype = ctypes.c_int
            gcast.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double, ctypes.c_double,
                ctypes.POINTER(ctypes.c_ubyte),
            ]
            gclose = lib.psunmask_gpu_close
            gclose.restype = None
            gclose.argtypes = [ctypes.c_void_p]
            api.update({"gpu_open": gopen, "gpu_cast": gcast, "gpu_close": gclose})
        except AttributeError:
            pass
        gs.verbose(f"libpsunmask loaded from {path}")
        return api
    return None


def _ram_available_mb():
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) // 1024
    except Exception:
        pass
    return 0


def _run_inram(dem_work, opt_pfx, opt_nsteps, opt_min_el, body, opt_scan_res,
               use_spice, use_meeus, sun_pos_fn, jd_start, et_start,
               spice_target, spice_frame, center_lat, center_lon,
               total_days, api, backend_pref="auto"):
    """Vectorised illumination accumulation via in-RAM ctypes shadow casts.

    `api` is the dict returned by _libpsunmask_load. `backend_pref` is
    'auto' | 'cpu' | 'gpu' — 'auto' tries GPU first and falls back to CPU.
    """
    import time as _t

    reg   = gs.region()
    nrows = int(reg["rows"]); ncols = int(reg["cols"])
    ewres = float(reg["ewres"]); nsres = float(reg["nsres"])

    gs.message(f"Loading DEM into RAM ({nrows*ncols*4/1e6:.0f} MB)…")
    dem = _np.asarray(_garray.array(mapname=dem_work, dtype=_np.float32),
                      dtype=_np.float32)
    nan_mask = _np.isnan(dem)
    # libpsunmask compares h == nodata; NaN comparisons are always false, so
    # replace nulls with a sentinel that's also passed as `nodata` to the lib.
    SENTINEL = _np.float32(-3.4028235e38)
    if nan_mask.any():
        dem[nan_mask] = SENTINEL
    dem = _np.ascontiguousarray(dem)
    mask_buf = _np.empty(dem.shape, dtype=_np.uint8)
    accum    = _np.zeros(dem.shape, dtype=_np.uint16)
    max_acc  = _np.zeros(dem.shape, dtype=_np.uint8)
    dem_ptr  = dem.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    mask_ptr = mask_buf.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))

    # ── pick GPU or CPU backend ──────────────────────────────────────────
    gpu_ctx = None
    if backend_pref in ("auto", "gpu") and api.get("gpu_open"):
        err = ctypes.create_string_buffer(512)
        gpu_ctx = api["gpu_open"](dem_ptr, nrows, ncols, ewres, nsres,
                                   ctypes.c_float(SENTINEL), err, 512)
        if gpu_ctx:
            gs.message(f"GPU backend active: {err.value.decode(errors='replace')}")
        else:
            msg = err.value.decode(errors='replace')
            if backend_pref == "gpu":
                gs.fatal(f"GPU backend requested but unavailable: {msg}")
            gs.message(f"GPU unavailable ({msg}); using CPU/OpenMP backend.")
    else:
        gs.message(f"Backend: CPU/OpenMP (preference={backend_pref}).")

    dt = total_days / opt_nsteps
    gs.message(f"Simulating {opt_nsteps} steps over {total_days:.1f} days "
               f"(dt={dt:.2f} d/step, in-RAM)…")

    cast_cpu = api["cast"]
    cast_gpu = api.get("gpu_cast")
    n_above = 0
    t0 = _t.time()
    try:
        for i in range(opt_nsteps):
            t = i * dt
            if use_spice:
                sub_lat, sub_lon = p_spice.subsolar_point(
                    spice_target, spice_frame, et_start + t * 86400.0)
            elif use_meeus:
                sub_lat, sub_lon = sun_position_meeus(jd_start + t, body)
            else:
                sub_lat, sub_lon = sun_pos_fn(t, body)
            elev, azim = solar_elevation_azimuth(
                center_lat, center_lon, sub_lat, sub_lon)
            if elev <= opt_min_el:
                continue
            alt = float(min(elev, 89.999)); az = float(azim)
            if gpu_ctx:
                rc = cast_gpu(gpu_ctx, alt, az, mask_ptr)
                if rc != 0:
                    gs.warning(f"GPU cast failed (rc={rc}) at step {i}; "
                               "falling back to CPU for the remainder.")
                    api["gpu_close"](gpu_ctx); gpu_ctx = None
                    cast_cpu(dem_ptr, nrows, ncols, ewres, nsres, alt, az,
                             ctypes.c_float(SENTINEL), mask_ptr)
            else:
                cast_cpu(dem_ptr, nrows, ncols, ewres, nsres, alt, az,
                         ctypes.c_float(SENTINEL), mask_ptr)
            lit = (mask_buf == 1)              # exclude 255=nodata
            accum += lit
            _np.maximum(max_acc, lit.view(_np.uint8), out=max_acc)
            n_above += 1
            step = max(1, opt_nsteps // 20)
            if (i + 1) % step == 0:
                gs.percent(i + 1, opt_nsteps, 1)
    finally:
        if gpu_ctx and api.get("gpu_close"):
            api["gpu_close"](gpu_ctx)
    gs.percent(opt_nsteps, opt_nsteps, 1)
    elapsed = _t.time() - t0
    gs.message(f"Steps with sun above horizon: {n_above}/{opt_nsteps}. "
               f"(in-RAM, {elapsed:.1f} s, {elapsed/max(1,n_above):.2f} s/step)")
    if n_above == 0:
        gs.fatal("Sun never above min_elevation at region centre.")

    frac = accum.astype(_np.float32) / float(n_above)
    if nan_mask.any():
        frac[nan_mask] = _np.nan
        max_acc_f = max_acc.astype(_np.float32)
        max_acc_f[nan_mask] = _np.nan
    else:
        max_acc_f = max_acc.astype(_np.float32)

    frac_out = f"{opt_pfx}_fraction"
    g = _garray.array(dtype=_np.float32); g[...] = frac
    g.write(mapname=frac_out, overwrite=gs.overwrite())
    gs.run_command("r.support", map=frac_out,
                   title="Solar illumination fraction [0=always dark, 1=always lit]",
                   units="fraction",
                   source1="p.illumination.sunfraction (in-RAM)", quiet=True)
    gs.run_command("r.colors", map=frac_out, color="bcyr", quiet=True)

    psr_out = f"{opt_pfx}_psr"
    psr = _np.where(nan_mask, _np.nan, (frac == 0.0).astype(_np.float32))
    g = _garray.array(dtype=_np.float32); g[...] = psr
    g.write(mapname=psr_out, overwrite=gs.overwrite())
    gs.run_command("r.support", map=psr_out,
                   title="Permanently shadowed regions mask (1=PSR)",
                   source1="p.illumination.sunfraction (in-RAM)", quiet=True)

    max_out = f"{opt_pfx}_max"
    g = _garray.array(dtype=_np.float32); g[...] = max_acc_f
    g.write(mapname=max_out, overwrite=gs.overwrite())
    gs.run_command("r.support", map=max_out,
                   title="Maximum illumination in any single step",
                   source1="p.illumination.sunfraction (in-RAM)", quiet=True)


def _cleanup():
    cleanup_prefix(_PREFIX_TMP)


atexit.register(_cleanup)


def _center_latlon():
    """Return (lat, lon) of the current computational region centre."""
    reg = gs.region()
    lat = (reg["n"] + reg["s"]) / 2.0
    lon = (reg["e"] + reg["w"]) / 2.0
    return lat, lon


def main():
    opt_dem        = options["dem"]
    opt_body       = options["body"]
    opt_nsteps     = int(options["nsteps"])
    opt_min_el     = float(options["min_elevation"])
    opt_pfx        = options["prefix"]
    opt_sunmask    = options["sunmask_module"]
    opt_scan_res   = float(options.get("scan_res", 0) or 0)
    opt_ephem      = (options.get("ephemeris") or "auto").lower()
    opt_start_ep   = options.get("start_epoch") or ""
    flag_keep      = flags["k"]
    flag_chunked   = flags["c"]

    body = body_params(opt_body)
    pid  = os.getpid()

    # ── optional DEM resampling for faster shadow computation ─────────────
    if opt_scan_res > 0:
        gs.message(f"Resampling DEM to {opt_scan_res:.0f} m for shadow computation…")
        gs.use_temp_region()
        # Do NOT pass raster=opt_dem: if the in-mapset DEM is the full
        # polar cap (ldem_85s_20m, ldem_75s_30m), that would expand the
        # region from the caller's 15×15 km box to the entire cap before
        # applying res=opt_scan_res, blowing cell count ~400× and pushing
        # the in-RAM estimator off its fast path → chunked-subprocess
        # segfault. The caller already set the region to the JSON bounds.
        gs.run_command("g.region", res=opt_scan_res, flags="a", quiet=True)
        dem_work = f"{_PREFIX_TMP}dem_scan_{pid}"
        gs.run_command("r.resamp.stats", input=opt_dem, output=dem_work,
                       method="average", quiet=True, overwrite=True)
    else:
        dem_work = opt_dem

    # Choose the sub-solar point model. Cascade for ephemeris=auto:
    #   SPICE (if a mapset meta-kernel is configured and libcspice loads)
    #   → Meeus real ephemeris (Earth's Moon only)
    #   → analytic single-sine toy model (any body).
    body_name = body.get("name", "").lower()
    epoch = opt_start_ep or body.get("start_epoch") or "2000-01-01T12:00:00"
    use_spice = use_meeus = False
    jd_start = et_start = None
    spice_target = spice_frame = None

    if opt_ephem in ("auto", "spice"):
        cfg = None
        if p_spice.spice_available():
            try:
                cfg = p_spice.activate_from_mapset()
            except Exception as e:
                gs.warning(f"SPICE activation failed: {e}")
        if cfg:
            spice_target = cfg.get("P_SPICE_TARGET") or "MOON"
            spice_frame  = cfg.get("P_SPICE_FRAME") or "IAU_MOON"
            try:
                et_start = p_spice.str2et(epoch)
                use_spice = True
                gs.message(f"Sub-solar point: SPICE ({spice_target}/{spice_frame}) "
                           f"from epoch {epoch}.")
            except p_spice.SpiceError as e:
                gs.warning(f"SPICE time conversion failed: {e}")
        if not use_spice and opt_ephem == "spice":
            gs.fatal("ephemeris=spice requested but no usable SPICE configuration. "
                     "Run p.in.spice then p.spice.config, and ensure libcspice "
                     "is installed (or set $CSPICE_LIB).")

    if not use_spice and opt_ephem in ("auto", "meeus"):
        # Meeus is now defined for ALL bodies via p_lib.sun_position_meeus:
        # - Moon: full chapter 25+47+53 libration (~0.003° latitude)
        # - other bodies: J2000-anchored seasonal model with body-specific
        #   Ls calibration (~0.1° latitude). Supersedes the old toy
        #   sun_position_generic which had a phase-zero bug that put the
        #   sub-solar latitude at ~0 regardless of epoch on rotating
        #   bodies, breaking high-latitude landing-zone illumination
        #   (e.g. Enceladus south polar terrain in 2032).
        jd_start = iso_to_jd(epoch)
        use_meeus = True
        accuracy_note = ("(~0.003° on the Moon, ~0.1° on other bodies; "
                         "use ephemeris=spice for arcsecond accuracy)")
        gs.message(f"Sub-solar point: Meeus analytic ephemeris {accuracy_note} "
                   f"from epoch {epoch} (JD {jd_start:.1f}).")

    if not use_spice and not use_meeus:
        # Reached only when ephemeris=analytic is forced.
        sun_pos_fn = sun_position_moon if body_name == "moon" else sun_position_generic
        gs.message("Sub-solar point: analytic single-sine toy model "
                   "(ephemeris=analytic forced; consider ephemeris=meeus).")

    # Total simulation period: explicit window_days wins, else fall back to
    # the body's nutation cycle (or sidereal period if no nutation).
    opt_window = float(options.get("window_days", 0) or 0)
    if opt_window > 0:
        total_days = opt_window
        _window_note = f" (mission window starting {opt_start_ep or 'epoch 0'})"
    elif body.get("nutation_period_years", 0) > 0:
        total_days = body["nutation_period_years"] * 365.25
        _window_note = ""
    else:
        total_days = body.get("sidereal_period_days", 365.25)
        _window_note = ""

    dt = total_days / opt_nsteps
    gs.message(
        f"Simulating {opt_nsteps} steps over {total_days:.1f} days "
        f"(dt={dt:.2f} d/step){_window_note}…"
    )

    center_lat, center_lon = region_center_geographic()
    gs.verbose(f"Region centre: lat={center_lat:.2f}°, lon={center_lon:.2f}°")

    # ── path selection: in-RAM ctypes (fast) vs chunked subprocess ────────
    # The in-RAM path keeps the DEM, the per-step mask and the running
    # accumulators all in memory and calls libpsunmask.so once per step via
    # ctypes — skipping ~1000 process forks and ~1000 raster I/O round-trips
    # that otherwise dominate polar 30 m / 1000-step runs.
    use_inram = False
    api = None
    backend_pref = (os.environ.get("SUNMASK_BACKEND") or "auto").lower()
    if backend_pref not in ("auto", "cpu", "gpu"):
        gs.warning(f"SUNMASK_BACKEND={backend_pref!r} not recognised; using 'auto'.")
        backend_pref = "auto"
    if not flag_chunked and _HAVE_INRAM_DEPS:
        api = _libpsunmask_load()
        if api is not None:
            reg2 = gs.region()
            n_cells  = int(reg2["rows"]) * int(reg2["cols"])
            # DEM(4B) + accum(2B) + mask(1B) + max(1B) + scratch ≈ 9 B/cell.
            need_mb  = n_cells * 9 // (1024 * 1024)
            avail_mb = _ram_available_mb()
            if avail_mb >= need_mb * 3 // 2:
                gs.message(f"In-RAM fast path: ~{need_mb} MB working set, "
                           f"{avail_mb} MB available (backend pref={backend_pref}).")
                use_inram = True
            else:
                gs.message(f"RAM headroom too low for in-RAM path "
                           f"(need ~{need_mb*3//2} MB, have {avail_mb} MB); "
                           f"using chunked subprocess path.")
        else:
            gs.verbose("libpsunmask.so not found; using chunked subprocess path.")

    if use_inram:
        sun_fn = sun_pos_fn if not (use_spice or use_meeus) else None
        _run_inram(dem_work, opt_pfx, opt_nsteps, opt_min_el, body, opt_scan_res,
                   use_spice, use_meeus, sun_fn,
                   jd_start, et_start, spice_target, spice_frame,
                   center_lat, center_lon, total_days, api, backend_pref)
        if opt_scan_res > 0:
            gs.del_temp_region()
            gs.run_command("g.remove", type="raster", name=dem_work,
                           flags="f", quiet=True)
        gs.message("Output maps:")
        gs.message(f"{opt_pfx}_fraction")
        gs.message(f"{opt_pfx}_psr")
        gs.message(f"{opt_pfx}_max")
        return

    mask_maps   = []
    n_above_hor = 0

    for i in range(opt_nsteps):
        t = i * dt
        if use_spice:
            sub_lat, sub_lon = p_spice.subsolar_point(
                spice_target, spice_frame, et_start + t * 86400.0)
        elif use_meeus:
            sub_lat, sub_lon = sun_position_meeus(jd_start + t, body)
        else:
            sub_lat, sub_lon = sun_pos_fn(t, body)

        # Solar elevation at region centre (representative for the scene)
        elev, azim = solar_elevation_azimuth(
            center_lat, center_lon, sub_lat, sub_lon)

        if elev <= opt_min_el:
            continue   # sun below horizon at this time step — skip

        n_above_hor += 1
        mask_name = f"{_PREFIX_TMP}mask_{i:04d}_{pid}"

        gs.run_command(
            opt_sunmask,
            elevation=dem_work,
            output=mask_name,
            azimuth=azim,
            altitude=min(elev, 89.999),
            quiet=True,
            overwrite=True,
        )
        # Both r.sunmask and p.sunmask: 1=sunlit, 0/null=shadow.
        # Replace nulls with 0 so r.series can compute the mean.
        gs.run_command("r.null", map=mask_name, null=0, quiet=True)
        mask_maps.append(mask_name)

    gs.message(
        f"Steps with sun above horizon: {n_above_hor}/{opt_nsteps}. "
        f"Computing illumination fraction…"
    )

    if not mask_maps:
        gs.fatal("Sun never above min_elevation at region centre. "
                 "Check body parameters or study area location.")

    # ── bump file-descriptor soft limit toward the hard limit ────────────
    # r.series and r.horizon open ~3–5 files per input raster. Many Linux
    # setups have soft RLIMIT_NOFILE=1024 with hard limit far higher.
    try:
        import resource
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        target = hard if hard > soft else soft
        if target > soft:
            resource.setrlimit(resource.RLIMIT_NOFILE, (target, hard))
            gs.verbose(f"Raised RLIMIT_NOFILE from {soft} to {target}.")
    except Exception as e:
        gs.verbose(f"Could not raise RLIMIT_NOFILE: {e}")

    # ── streaming aggregation: fold one small chunk at a time into a ─────
    # running sum/max. At most chunk_size + 4 maps are ever open, so the
    # fd limit cannot be hit regardless of nsteps. Disk peak is also small
    # (one chunk's partials at a time) because we delete as we go.
    chunk_size = 50
    n_chunks = (len(mask_maps) + chunk_size - 1) // chunk_size
    gs.message(
        f"Aggregating {len(mask_maps)} per-step masks via streaming "
        f"accumulator ({n_chunks} chunk(s) of up to {chunk_size})…"
    )

    sum_acc = f"{_PREFIX_TMP}sum_acc_{pid}"
    max_acc = f"{_PREFIX_TMP}max_acc_{pid}"
    sum_tmp = f"{_PREFIX_TMP}sum_tmp_{pid}"
    max_tmp = f"{_PREFIX_TMP}max_tmp_{pid}"

    for ci in range(n_chunks):
        chunk = mask_maps[ci * chunk_size : (ci + 1) * chunk_size]

        # Per-chunk sum and max
        csum = f"{_PREFIX_TMP}csum_{ci:04d}_{pid}"
        cmax = f"{_PREFIX_TMP}cmax_{ci:04d}_{pid}"
        gs.run_command("r.series",
                       input=",".join(chunk), output=csum,
                       method="sum",     quiet=True, overwrite=True)
        gs.run_command("r.series",
                       input=",".join(chunk), output=cmax,
                       method="maximum", quiet=True, overwrite=True)

        # Drop the source masks immediately
        if not flag_keep:
            gs.run_command("g.remove", type="raster",
                           name=",".join(chunk), flags="f", quiet=True)

        # Fold this chunk into the running accumulator
        if ci == 0:
            gs.run_command("g.rename",
                           raster=f"{csum},{sum_acc}",
                           overwrite=True, quiet=True)
            gs.run_command("g.rename",
                           raster=f"{cmax},{max_acc}",
                           overwrite=True, quiet=True)
        else:
            gs.mapcalc(f"{sum_tmp} = {sum_acc} + {csum}",
                       overwrite=True, quiet=True)
            gs.mapcalc(f"{max_tmp} = max({max_acc}, {cmax})",
                       overwrite=True, quiet=True)
            gs.run_command("g.remove", type="raster",
                           name=f"{sum_acc},{max_acc},{csum},{cmax}",
                           flags="f", quiet=True)
            gs.run_command("g.rename",
                           raster=f"{sum_tmp},{sum_acc}",
                           overwrite=True, quiet=True)
            gs.run_command("g.rename",
                           raster=f"{max_tmp},{max_acc}",
                           overwrite=True, quiet=True)

        gs.percent(ci + 1, n_chunks, 1)

    # ── illumination fraction = total_sum / N_steps ──────────────────────
    frac_out = f"{opt_pfx}_fraction"
    gs.mapcalc(f"{frac_out} = double({sum_acc}) / {n_above_hor}",
               overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=frac_out,
                   title="Solar illumination fraction [0=always dark, 1=always lit]",
                   units="fraction",
                   source1="p.illumination.sunfraction", quiet=True)
    gs.run_command("r.colors", map=frac_out, color="bcyr", quiet=True)

    # ── permanently shadowed regions (PSR) ───────────────────────────────
    psr_out = f"{opt_pfx}_psr"
    gs.mapcalc(f"{psr_out} = if({frac_out} == 0, 1, 0)",
               overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=psr_out,
                   title="Permanently shadowed regions mask (1=PSR)",
                   source1="p.illumination.sunfraction", quiet=True)

    # ── max illumination across all timesteps ────────────────────────────
    max_out = f"{opt_pfx}_max"
    gs.run_command("g.rename",
                   raster=f"{max_acc},{max_out}",
                   overwrite=gs.overwrite(), quiet=True)
    gs.run_command("r.support", map=max_out,
                   title="Maximum illumination in any single step",
                   source1="p.illumination.sunfraction", quiet=True)

    # ── clean accumulator ────────────────────────────────────────────────
    gs.run_command("g.remove", type="raster",
                   name=sum_acc, flags="f", quiet=True)

    # ── restore region and clean resampled DEM if used ────────────────────
    if opt_scan_res > 0:
        gs.del_temp_region()
        gs.run_command("g.remove", type="raster",
                       name=dem_work, flags="f", quiet=True)

    gs.message("Output maps:")
    for m in [frac_out, psr_out, max_out]:
        gs.message(f"  {m}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
