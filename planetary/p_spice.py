"""
p_spice.py — thin ctypes wrapper around the adopted CSPICE shared library.

This is free and unencumbered software released into the public domain.
See https://unlicense.org for details.  (The CSPICE toolkit itself is
distributed under the NAIF license; see the cspice source tree.)

The wrapper deliberately exposes only the handful of CSPICE entry points the
planetary landing suite needs (kernel loading, time conversion, sub-observer
points and rectangular→latitudinal conversion).  It loads ``libcspice.so`` —
built from the adopted CSPICE C sources — via ctypes, so no spiceypy / pip
dependency is required.

Library search order:
  1. ``$CSPICE_LIB``                      (explicit override, full path)
  2. ``/usr/local/lib/libcspice.so``      (where the .deb installs it)
  3. ``ctypes.util.find_library("cspice")``
  4. ``$HOME/dev/cspice/build/libcspice.so``  (developer build tree)

Kernel cache location:
  Kernels and generated meta-kernels live under the GRASS *user config
  directory* — the directory that contains your ``GISRC`` file (typically
  ``~/.grass8/`` or ``~/.grass8.6/``) — in a ``p_spice/`` subdirectory.
  See ``spice_cache_dir()`` and the p.in.spice manual for the layout.
"""

import os
import ctypes
import ctypes.util
import math


# ── library loading ──────────────────────────────────────────────────────────

_LIB = None
_LIB_PATH = None


def _candidate_lib_paths():
    env = os.environ.get("CSPICE_LIB")
    if env:
        yield env
    yield "/usr/local/lib/libcspice.so"
    found = ctypes.util.find_library("cspice")
    if found:
        yield found
    yield os.path.expanduser("~/dev/cspice/build/libcspice.so")


def load_library():
    """Load and configure the CSPICE shared library (idempotent).

    Returns the ctypes CDLL handle, or raises OSError with the paths tried."""
    global _LIB, _LIB_PATH
    if _LIB is not None:
        return _LIB

    tried = []
    for path in _candidate_lib_paths():
        tried.append(path)
        if path and (os.path.isfile(path) or "/" not in path):
            try:
                lib = ctypes.CDLL(path)
            except OSError:
                continue
            _configure_signatures(lib)
            # Make SPICE return on error instead of aborting the process,
            # and silence its console error printer (we read getmsg_c).
            lib.erract_c(b"SET", 0, b"RETURN")
            lib.errprt_c(b"SET", 0, b"NONE")
            _LIB, _LIB_PATH = lib, path
            return lib
    raise OSError("libcspice.so not found. Tried: " + ", ".join(t for t in tried if t))


def spice_available():
    """True if the CSPICE shared library can be loaded."""
    try:
        load_library()
        return True
    except OSError:
        return False


def library_path():
    """Path of the loaded library, or None."""
    return _LIB_PATH


_D3 = ctypes.c_double * 3


def _configure_signatures(lib):
    d = ctypes.c_double
    dp = ctypes.POINTER(ctypes.c_double)
    cp = ctypes.c_char_p
    ci = ctypes.c_int
    lib.furnsh_c.argtypes = [cp]
    lib.unload_c.argtypes = [cp]
    lib.kclear_c.argtypes = []
    lib.str2et_c.argtypes = [cp, dp]
    lib.et2utc_c.argtypes = [d, cp, ci, ci, ctypes.c_char_p]
    lib.subslr_c.argtypes = [cp, cp, d, cp, cp, cp, _D3, dp, _D3]
    lib.subpnt_c.argtypes = [cp, cp, d, cp, cp, cp, _D3, dp, _D3]
    lib.reclat_c.argtypes = [_D3, dp, dp, dp]
    lib.erract_c.argtypes = [cp, ci, cp]
    lib.errprt_c.argtypes = [cp, ci, cp]
    lib.failed_c.restype = ci
    lib.reset_c.argtypes = []
    lib.getmsg_c.argtypes = [cp, ci, ctypes.c_char_p]


# ── error handling ─────────────────────────────────────────────────────────

class SpiceError(RuntimeError):
    pass


def _check():
    """Raise SpiceError carrying the CSPICE long message if a call failed."""
    if _LIB.failed_c():
        buf = ctypes.create_string_buffer(1841)
        _LIB.getmsg_c(b"LONG", 1841, buf)
        msg = buf.value.decode(errors="replace")
        _LIB.reset_c()
        raise SpiceError(msg or "unknown SPICE error")


# ── low-level entry points ──────────────────────────────────────────────────

def furnsh(path):
    load_library()
    _LIB.furnsh_c(str(path).encode())
    _check()


def unload(path):
    load_library()
    _LIB.unload_c(str(path).encode())
    _check()


def kclear():
    load_library()
    _LIB.kclear_c()


def str2et(utc):
    """UTC string → ephemeris time (TDB seconds past J2000)."""
    load_library()
    et = ctypes.c_double()
    _LIB.str2et_c(str(utc).encode(), ctypes.byref(et))
    _check()
    return et.value


def _subpoint(fn, target, et, fixref, obsrvr, abcorr, method):
    load_library()
    spoint = _D3()
    trgepc = ctypes.c_double()
    srfvec = _D3()
    fn(method.encode(), str(target).encode(), ctypes.c_double(et),
       str(fixref).encode(), abcorr.encode(), str(obsrvr).encode(),
       spoint, ctypes.byref(trgepc), srfvec)
    _check()
    return [spoint[0], spoint[1], spoint[2]]


def reclat(vec):
    """Rectangular → latitudinal. Returns (lat_deg, lon_deg) with lon in
    (-180, 180], east-positive (CSPICE convention)."""
    load_library()
    arr = _D3(*vec)
    r = ctypes.c_double()
    lon = ctypes.c_double()
    lat = ctypes.c_double()
    _LIB.reclat_c(arr, ctypes.byref(r), ctypes.byref(lon), ctypes.byref(lat))
    _check()
    return math.degrees(lat.value), math.degrees(lon.value)


# ── high-level sub-observer points ──────────────────────────────────────────

def subsolar_point(target, fixref, et, abcorr="LT+S", method="Near point: ellipsoid"):
    """Selenographic/planetocentric (lat, lon_east_0_360) of the sub-solar
    point of `target` (e.g. 'MOON') in body-fixed frame `fixref` (e.g.
    'MOON_ME' or 'IAU_MOON') at ephemeris time `et`."""
    pt = _subpoint(_LIB.subslr_c, target, et, fixref, "SUN", abcorr, method)
    lat, lon = reclat(pt)
    return lat, lon % 360.0


def subobserver_point(target, fixref, et, observer="EARTH",
                      abcorr="LT+S", method="Near point: ellipsoid"):
    """Selenographic/planetocentric (lat, lon_east_0_360) of the sub-observer
    point — the point on `target` directly beneath `observer` (default Earth).
    For the Moon this is the libration / sub-Earth point."""
    pt = _subpoint(_LIB.subpnt_c, target, et, fixref, observer, abcorr, method)
    lat, lon = reclat(pt)
    return lat, lon % 360.0


# ── cache / config-directory resolution ─────────────────────────────────────

def grass_config_dir():
    """The GRASS user config directory (the one containing GISRC), or a
    sensible default outside a GRASS session."""
    gisrc = os.environ.get("GISRC")
    if gisrc and os.path.dirname(gisrc):
        return os.path.dirname(gisrc)
    # Outside GRASS: pick the highest-versioned ~/.grass8* dir if any exist.
    home = os.path.expanduser("~")
    candidates = sorted(
        d for d in os.listdir(home)
        if d.startswith(".grass") and os.path.isdir(os.path.join(home, d))
    )
    if candidates:
        return os.path.join(home, candidates[-1])
    return os.path.join(home, ".grass8")


def spice_cache_dir():
    """Root of the SPICE cache: ``<grass-config>/p_spice``.
    Honours the ``$P_SPICE_CACHE`` override."""
    override = os.environ.get("P_SPICE_CACHE")
    if override:
        return override
    return os.path.join(grass_config_dir(), "p_spice")


def kernels_dir():
    return os.path.join(spice_cache_dir(), "kernels")


def meta_dir():
    return os.path.join(spice_cache_dir(), "meta")


# ── mapset configuration bridge (lazy GRASS import) ─────────────────────────

_MAPSET_KEYS = ("P_SPICE_META", "P_SPICE_TARGET",
                "P_SPICE_FRAME", "P_SPICE_OBSERVER")


def read_mapset_config():
    """Return the per-mapset SPICE config dict (keys as set by p.spice.config).
    Only callable inside a GRASS session."""
    import grass.script as gs
    # Read the whole mapset store in one call. Querying an individual unset
    # variable with `g.gisenv get=...` exits non-zero and prints a scary
    # ERROR; dumping the store never errors and lists only the set keys.
    dump = gs.read_command("g.gisenv", store="mapset")
    present = {}
    for line in dump.splitlines():
        if "=" in line:
            key, _, val = line.partition("=")
            present[key.strip()] = val.strip().strip("'\"")
    return {k: (present.get(k) or None) for k in _MAPSET_KEYS}


def activate_from_mapset():
    """Load the meta-kernel recorded in the current mapset's SPICE config.
    Returns the config dict, or None if no meta-kernel is configured."""
    cfg = read_mapset_config()
    meta = cfg.get("P_SPICE_META")
    if not meta:
        return None
    kclear()
    furnsh(meta)
    return cfg
