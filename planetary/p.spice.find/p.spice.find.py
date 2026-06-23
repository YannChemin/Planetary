#!/usr/bin/env python3
############################################################################
# MODULE:       p.spice.find
# PURPOSE:      Find and download NAIF SPICE kernels (CK, SPK, SCLK, IK,
#               FK, PCK, LSK) for a spacecraft + time window from the NAIF
#               anonymous FTP/HTTP server.  Parses the NAIF directory
#               listings; no meta-index required.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Find and download NAIF SPICE kernels for a spacecraft and time window.
# % keyword: Planetary
# % keyword: SPICE & Ephemeris
# % keyword: import
# % keyword: kernels
# %end

# %option
# % key: spacecraft
# % type: string
# % label: Spacecraft name as known to NAIF (e.g. CASSINI, MRO, LRO, VEX)
# % required: yes
# %end

# %option
# % key: instrument
# % type: string
# % label: Instrument name, for instruments needing extra kernels beyond the spacecraft default
# % description: Currently supported: CRISM (MRO) -- fetches the gimbal/articulation CK (mro_crm_*, separate from the regular spacecraft-body CK), the virtual SCLK (*.65536.tsc, separate from the regular spacecraft SCLK), and (with kernels=...,iak) the crismAddendum IAK that CRISM's camera model needs. ISS_NAC, ISS_WAC (CASSINI) -- fetches the IssNAAddendum/IssWAAddendum IAK (with kernels=...,iak) that their pinhole camera models need.
# % required: no
# %end

# %option
# % key: time
# % type: string
# % label: UTC time of interest (ISO 8601: YYYY-MM-DDTHH:MM:SS)
# % required: yes
# %end

# %option
# % key: kernels
# % type: string
# % label: Comma-separated kernel types to fetch
# % description: Supported: lsk,sclk,ik,fk,pck,spk,ck,iak. iak (instrument addendum kernel: BORESIGHT/PIXEL_PITCH/FOCAL_LENGTH for camera models) is NOT a real SPICE kernel type and is never on naif.jpl.nasa.gov -- it is fetched from the ISIS3 project's own public AWS data mirror instead (see -l output for the exact source URL).
# % answer: lsk,sclk,ik,fk,pck,spk,ck
# % required: no
# %end

# %option
# % key: dest
# % type: string
# % label: Destination root directory (kernel type subdirectories created automatically)
# % description: Default: spice/ inside the active GRASS mapset. Modules check this cache first before downloading.
# % required: no
# %end

# %option
# % key: ck_type
# % type: string
# % label: CK file preference: ra=reconstructed-actual, ca_ISS=camera-adjusted, pa=predict
# % options: ra,ca_ISS,pa,any
# % answer: ra
# % required: no
# %end

# %option
# % key: timeout
# % type: integer
# % label: Per-file download timeout (seconds)
# % answer: 600
# % required: no
# %end

# %flag
# % key: l
# % description: List matching kernel filenames, do not download
# %end

# %flag
# % key: f
# % description: Force re-download even if file already exists
# %end

# %flag
# % key: m
# % description: Write a SPICE meta-kernel (.tm) in dest referencing downloaded files
# %end

import os
import sys
import re
import datetime
import urllib.request
import html.parser

import grass.script as gs

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_spice

# ---------------------------------------------------------------------------
# NAIF server root
# ---------------------------------------------------------------------------
NAIF_ROOT = "https://naif.jpl.nasa.gov/pub/naif"

# ---------------------------------------------------------------------------
# ISIS3 project's own public AWS data mirror: the only source for instrument
# addendum kernels (IAK -- BORESIGHT_LINE/SAMPLE, PIXEL_PITCH, FOCAL_LENGTH;
# an ISIS3 concept, not a NAIF/SPICE one, so never on naif.jpl.nasa.gov).
# Found by reading ISIS3's own isis/scripts/downloadIsisData and
# isis/config/rclone.conf: the "<mission>_usgs" rclone remotes are aliases
# for this bucket, browsable over plain HTTPS with no AWS credentials.
# ---------------------------------------------------------------------------
AWS_ISIS_DATA = "https://asc-isisdata.s3.us-west-2.amazonaws.com"

# Map a SPACECRAFT["dir"] (NAIF directory name) to this bucket's mission
# slug. Confirmed live by listing the bucket's usgs_data/ prefix -- not
# every NAIF mission has a slug here (e.g. Venus Express/VEX does not).
AWS_MISSION_DIR = {
    "MRO":        "mro",
    "CASSINI":    "cassini",
    "LRO":        "lro",
    "MESSENGER":  "messenger",
}

# ---------------------------------------------------------------------------
# Spacecraft database: name -> (NAIF-ID, body/mission-dir, SCLK-glob-hint,
#                                IK-glob-hints, FK-glob-hint)
# ---------------------------------------------------------------------------
SPACECRAFT = {
    "CASSINI": {
        "id":      -82,
        "dir":     "CASSINI",
        "body":    "Saturn",
        "sclk":    "cas00172.tsc",
        "ik":      "cas_iss_v10.ti",
        "fk":      "cas_v*",   # latest versioned spacecraft FK
        "pck":     ["cpck_rock_21Jan2011_merged.tpc", "pck00010.tpc"],
    },
    "MRO": {
        "id":      -74,
        "dir":     "MRO",
        "body":    "Mars",
        "sclk":    None,  # latest in sclk/
        "ik":      None,
        "fk":      None,
        "pck":     ["pck00010.tpc"],
    },
    "LRO": {
        "id":      -85,
        "dir":     "LRO",
        "body":    "Moon",
        "sclk":    None,
        "ik":      None,
        "fk":      None,
        "pck":     ["pck00010.tpc"],
    },
    "MESSENGER": {
        "id":      -236,
        "dir":     "MESSENGER",
        "body":    "Mercury",
        "sclk":    None,
        "ik":      None,
        "fk":      None,
        "pck":     ["pck00010.tpc"],
    },
    "VEX": {
        "id":      -248,
        "dir":     "VEX",
        "body":    "Venus",
        "sclk":    None,
        "ik":      None,
        "fk":      None,
        "pck":     ["pck00010.tpc"],
    },
    "MEX": {
        "id":      -41,
        "dir":     "MEX",
        "body":    "Mars",
        "sclk":    None,  # single "STEP" SCLK, latest in sclk/, no date-range to pick
        "ik":      None,
        "fk":      None,
        "pck":     ["pck00010.tpc"],
    },
}

# ---------------------------------------------------------------------------
# Per-instrument kernel knowledge, for instruments whose own pointing isn't
# fully covered by the spacecraft's regular kernels. Confirmed live against
# naif.jpl.nasa.gov: CRISM's gimbal/articulation frame (MRO_CRISM_ART, NAIF
# ID -74012) is driven by a CK that lives in the same MRO/kernels/ck/
# directory as the regular spacecraft-body CK but with a different filename
# prefix (mro_crm_* vs mro_sc_psp_*), and is decoded via a virtual SCLK id
# (-74999) whose kernel lives in the same MRO/kernels/sclk/ directory as the
# regular spacecraft SCLK but with a ".65536.tsc" suffix instead of plain
# ".tsc". Both confirmed by directory listing, not guessed.
# ---------------------------------------------------------------------------
INSTRUMENT = {
    "CRISM": {
        "spacecraft":      "MRO",
        "ik":              "mro_crism_v10.ti",
        "extra_ck_prefix": "mro_crm_",
        "extra_sclk_substr": ".65536.",
        "iak_prefix":      "crismAddendum",
    },
    "ISS_NAC": {
        "spacecraft":      "CASSINI",
        "ik":              "cas_iss_v10.ti",
        "iak_prefix":      "IssNAAddendum",
    },
    "ISS_WAC": {
        "spacecraft":      "CASSINI",
        "ik":              "cas_iss_v10.ti",
        "iak_prefix":      "IssWAAddendum",
    },
    "VIMS": {
        "spacecraft":      "CASSINI",
        "ik":              "cas_vims_v*",
        # NOTE: vimsAddendum*.ti only fixes CK_FRAME_ID/NAIF_BODY_CODE
        # housekeeping, not BORESIGHT/FOCAL_LENGTH -- VIMS's real per-pixel
        # geometry is a 2-D scan-mirror mapping, not a pinhole; no camera
        # model uses this yet (see TODO.md).
        "iak_prefix":      "vimsAddendum",
    },
}

# ---------------------------------------------------------------------------
# Filename time-range parsers
# ---------------------------------------------------------------------------

def _yydoy_to_date(yy, doy):
    """Convert 2-digit year + DOY to a date. Pivot: yy < 70 -> 2000s, else 1900s."""
    year = 2000 + yy if yy < 70 else 1900 + yy
    return datetime.date(year, 1, 1) + datetime.timedelta(days=doy - 1)


def _yymmdd_to_date(yy, mm, dd):
    year = 2000 + yy if yy < 70 else 1900 + yy
    return datetime.date(year, mm, dd)


# Patterns tried in order; each returns (start_date, end_date) or None.
_RE_YYDOY_YYDOY   = re.compile(r'^(\d{2})(\d{3})_(\d{2})(\d{3})')
_RE_YYMMDD_YYMMDD = re.compile(r'^(\d{2})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})')
# SPK: YYMMDD{letters}_CLASS_YYDOY_YYDOY  e.g. 040701AP_SCPSE_04173_04236.bsp
_RE_SPK_COVERAGE  = re.compile(r'^\d{6}[A-Z]+_[A-Z0-9]+_(\d{2})(\d{3})_(\d{2})(\d{3})')


def _file_date_range(name):
    """Return (start_date, end_date) from a NAIF CK/SPK filename, or None."""
    # SPK compound format first (YYMMDDTYPE_CLASS_YYDOY_YYDOY)
    m = _RE_SPK_COVERAGE.match(name.upper())
    if m:
        y1, doy1, y2, doy2 = (int(x) for x in m.groups())
        try:
            return _yydoy_to_date(y1, doy1), _yydoy_to_date(y2, doy2)
        except ValueError:
            pass
    # YYMMDD_YYMMDD must be tried before YYDOY_YYDOY (6+6 digits vs 5+5)
    m = _RE_YYMMDD_YYMMDD.match(name)
    if m:
        y1, mo1, d1, y2, mo2, d2 = (int(x) for x in m.groups())
        try:
            return _yymmdd_to_date(y1, mo1, d1), _yymmdd_to_date(y2, mo2, d2)
        except ValueError:
            pass
    m = _RE_YYDOY_YYDOY.match(name)
    if m:
        y1, doy1, y2, doy2 = (int(x) for x in m.groups())
        try:
            return _yydoy_to_date(y1, doy1), _yydoy_to_date(y2, doy2)
        except ValueError:
            pass
    return None


# ---------------------------------------------------------------------------
# HTML directory listing parser
# ---------------------------------------------------------------------------

class _LinkParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            for k, v in attrs:
                if k == "href" and v:
                    self.links.append(v)


def _list_dir(url, timeout=30):
    """Return list of filenames (not dirs) from an NAIF HTTP directory."""
    req = urllib.request.Request(url, headers={"User-Agent": "p.spice.find/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode("utf-8", errors="replace")
    p = _LinkParser()
    p.feed(body)
    files = []
    for link in p.links:
        # Strip leading path, keep only bare filename
        name = link.split("/")[-1]
        if name and "?" not in name and not name.startswith(".."):
            files.append(name)
    return files


_RE_S3_KEY = re.compile(r"<Key>([^<]+)</Key>")


def _list_s3_dir(prefix, timeout=30):
    """Return list of bare filenames under an asc-isisdata S3 prefix.

    Uses the public, unauthenticated S3 REST list API (XML), not rclone --
    no credentials needed for this bucket over plain HTTPS."""
    url = f"{AWS_ISIS_DATA}/?list-type=2&prefix={prefix}"
    req = urllib.request.Request(url, headers={"User-Agent": "p.spice.find/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode("utf-8", errors="replace")
    return [k.split("/")[-1] for k in _RE_S3_KEY.findall(body) if k.split("/")[-1]]


def _download(url, dst, timeout, force):
    if os.path.exists(dst) and not force:
        gs.verbose(f"  already present: {dst}")
        return 0
    tmp = dst + ".part"
    gs.message(f"  downloading {os.path.basename(dst)} …")
    req = urllib.request.Request(url, headers={"User-Agent": "p.spice.find/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        total = int(r.headers.get("Content-Length", 0))
        done = 0
        with open(tmp, "wb") as out:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
                done += len(chunk)
                if total:
                    gs.percent(done, total, 5)
    os.replace(tmp, dst)
    return done


# ---------------------------------------------------------------------------
# CK / SPK selection
# ---------------------------------------------------------------------------

_CK_PRIO = {
    "ra":     0,   # reconstructed actual — best
    "py":     1,   # as-flown reconstructed
    "ca":     2,   # camera-adjusted (ISS etc.)
    "pa":     3,   # predict
    "pd":     4,
    "pe":     5,
    "pf":     6,
    "pg":     7,
}


_RE_CK_TYPE = re.compile(r'^(\d+)([a-z]+)', re.IGNORECASE)


def _ck_type_score(name, pref):
    """Return (preference_score, span_days) for a CK basename; lower is better."""
    r = _file_date_range(name)
    span = (r[1] - r[0]).days if r else 9999

    # Type code: alphabetic tail of the second YYDOY/YYMMDD token.
    # e.g. '04183_04185ra.bc' → second token '04185ra' → type 'ra'
    #      '04171_04212py_as_flown.bc' → '04212py' → 'py'
    stem = name.rsplit(".", 1)[0]
    parts = stem.split("_")
    m = _RE_CK_TYPE.match(parts[1]) if len(parts) >= 2 else None
    type_code = m.group(2)[:2].lower() if m and m.group(2) else "zz"

    if pref != "any" and not type_code.startswith(pref[:2]):
        return 1000, span       # wrong preferred type
    prio = _CK_PRIO.get(type_code, 50)
    return prio, span


_CK_SKIP = re.compile(
    r'(scale.factor|gapfill|waypoint|wayp|lmb|itl|fsds|opnav|prly)',
    re.IGNORECASE,
)


def _trailing_edge_risk(r, target_date):
    """Return 1 if target_date is exactly the *last* nominal day of a
    CK/SPK file's date range, else 0.

    Real archived "ra"-reconstructed CK/SPK files are released in
    fixed-cadence windows (e.g. 5 days) whose *actual* data coverage
    runs from day-N 00:01 to day-(N+5) 00:01 -- NOT through the end of
    day N+5 despite the filename implying otherwise (confirmed live via
    ckcov_c/spkcov_c against real CASSINI archive files -- see TODO.md).
    Consecutive release windows' filename date ranges overlap by
    exactly one day at this boundary (e.g. "..._05292_05297..." and
    "..._05297_05302..."), so a target time late on that shared day
    needs the *next* file, which actually has data for the whole day,
    not the one whose range nominally ends there. This is a real,
    confirmed risk flag, not a guess -- used only to break ties when
    another candidate without the risk exists; it never excludes a
    candidate outright (still useful when it's the only match, e.g. for
    a time genuinely in the first few minutes of that day)."""
    return 1 if target_date >= r[1] else 0


def _best_ck(files, target_date, pref, name_prefix=None):
    """Return the best-matching CK filename covering target_date.

    *name_prefix*, when given, restricts candidates to filenames starting
    with it (e.g. "mro_crm_" to select CRISM's gimbal/articulation CK
    instead of the regular spacecraft-body CK, which lives in the same
    NAIF ck/ directory under a different filename prefix)."""
    candidates = []
    for f in files:
        if not f.endswith(".bc") or f.endswith(".lbl"):
            continue
        if name_prefix and not f.startswith(name_prefix):
            continue
        if _CK_SKIP.search(f):
            continue
        r = _file_date_range(f)
        if r is None:
            continue
        if r[0] <= target_date <= r[1]:
            score, span = _ck_type_score(f, pref)
            edge_risk = _trailing_edge_risk(r, target_date)
            candidates.append((score, edge_risk, span, f))
    if not candidates:
        return None
    candidates.sort()
    return candidates[0][3]


def _best_spk(files, target_date):
    """Return the best SPK covering target_date (prefer SCPSE, shortest span)."""
    candidates = []
    for f in files:
        if not f.endswith(".bsp"):
            continue
        r = _file_date_range(f)
        if r is None:
            continue
        if r[0] <= target_date <= r[1]:
            span = (r[1] - r[0]).days
            # Prefer SCPSE (spacecraft + planets + satellites in one file)
            scpse_bonus = 0 if "SCPSE" in f.upper() else 10
            edge_risk = _trailing_edge_risk(r, target_date)
            candidates.append((scpse_bonus, edge_risk, span, f))
    if not candidates:
        return None
    candidates.sort()
    return candidates[0][3]


# ---------------------------------------------------------------------------
# Latest-file finders (for SCLK, IK, FK, PCK, LSK)
# ---------------------------------------------------------------------------

def _latest_file(files, ext, hint=None):
    """Return the best-matching file with given extension.

    If hint ends with '*', treat it as a prefix filter and pick the
    last-sorted match.  If hint is an exact name, return it if present.
    Without a hint, return the last-sorted file with the extension.
    """
    if hint:
        if hint.endswith("*"):
            prefix = hint[:-1]
            matches = sorted(f for f in files if f.startswith(prefix) and f.endswith(ext))
            return matches[-1] if matches else None
        for f in files:
            if f == hint:
                return f
    matches = [f for f in files if f.endswith(ext)]
    return sorted(matches)[-1] if matches else None


# ---------------------------------------------------------------------------
# Meta-kernel writer
# ---------------------------------------------------------------------------

def _write_metakernel(mk_path, kernel_paths, spacecraft, target_date):
    lines = [
        r"KPL/MK",
        r"",
        f"\\begintext",
        f"  Auto-generated by p.spice.find",
        f"  Spacecraft : {spacecraft}",
        f"  Date       : {target_date}",
        r"",
        r"\\begindata",
        r"",
        r"  KERNELS_TO_LOAD = (",
    ]
    for p in kernel_paths:
        lines.append(f"    '{p}'")
    lines += ["  )", "", "\\begintext", ""]
    with open(mk_path, "w") as f:
        f.write("\n".join(lines))
    gs.message(f"Meta-kernel written: {mk_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    sc_name  = options["spacecraft"].upper()
    instrument = (options["instrument"] or "").upper() or None
    time_str = options["time"]
    ktypes   = [k.strip() for k in options["kernels"].split(",")]
    dest     = options["dest"]
    ck_pref  = options["ck_type"]
    timeout  = int(options["timeout"])
    flag_list  = flags["l"]
    flag_force = flags["f"]
    flag_meta  = flags["m"]

    if sc_name not in SPACECRAFT:
        known = ", ".join(sorted(SPACECRAFT.keys()))
        gs.fatal(f"Unknown spacecraft '{sc_name}'. Supported: {known}")

    instr = None
    if instrument:
        if instrument not in INSTRUMENT:
            known = ", ".join(sorted(INSTRUMENT.keys()))
            gs.fatal(f"Unknown instrument '{instrument}'. Supported: {known}")
        instr = INSTRUMENT[instrument]
        if instr["spacecraft"] != sc_name:
            gs.fatal(f"instrument={instrument} requires "
                     f"spacecraft={instr['spacecraft']}, not {sc_name}.")

    sc = SPACECRAFT[sc_name]
    sc_dir = sc["dir"]

    # Parse UTC time
    # Only the date matters for kernel selection; strip any sub-second
    # fraction (e.g. real PDS3 START_TIME values like
    # "2007-01-05T01:26:56.855") before matching, rather than rejecting it.
    time_str_nofrac = re.sub(r"(\d{2}:\d{2}:\d{2})\.\d+", r"\1", time_str)
    for fmt in ("%Y-%m-%dT%H:%M:%S", "%Y-%m-%d", "%Y-%jT%H:%M:%S"):
        try:
            dt = datetime.datetime.strptime(time_str_nofrac, fmt)
            break
        except ValueError:
            pass
    else:
        gs.fatal(f"Cannot parse time '{time_str}'. Use YYYY-MM-DDTHH:MM:SS.")
    target_date = dt.date()

    # Destination: default to mapset-local spice/ directory so each project
    # carries its own kernel set and modules can check a local cache first.
    if not dest:
        dest = p_spice.mapset_spice_dir()
    dest = os.path.expanduser(dest)

    base_url = f"{NAIF_ROOT}/{sc_dir}/kernels"
    downloaded = []

    def _fetch(ktype, subdir, filename):
        url = f"{base_url}/{subdir}/{filename}"
        out_dir = os.path.join(dest, ktype)
        os.makedirs(out_dir, exist_ok=True)
        dst = os.path.join(out_dir, filename)
        if flag_list:
            gs.message(f"  [{ktype}] {filename}  ({url})")
            return dst
        _download(url, dst, timeout, flag_force)
        return dst

    # ── LSK (from generic_kernels, not spacecraft-specific) ───────────────
    if "lsk" in ktypes:
        gs.message("Finding LSK …")
        files = _list_dir(f"{NAIF_ROOT}/generic_kernels/lsk/", timeout)
        fn = _latest_file(files, ".tls", "naif0012.tls")
        if fn:
            out_dir = os.path.join(dest, "lsk")
            os.makedirs(out_dir, exist_ok=True)
            dst = os.path.join(out_dir, fn)
            if flag_list:
                gs.message(f"  [lsk] {fn}  ({NAIF_ROOT}/generic_kernels/lsk/{fn})")
            else:
                _download(f"{NAIF_ROOT}/generic_kernels/lsk/{fn}", dst, timeout, flag_force)
            downloaded.append(dst)
        else:
            gs.warning("No LSK (.tls) found.")

    # ── SCLK ──────────────────────────────────────────────────────────────
    if "sclk" in ktypes:
        gs.message("Finding SCLK …")
        files = _list_dir(f"{base_url}/sclk/", timeout)
        fn = _latest_file(files, ".tsc", sc.get("sclk"))
        if fn:
            p = _fetch("sclk", "sclk", fn)
            downloaded.append(p)
        else:
            gs.warning("No SCLK (.tsc) found.")

        if instr and instr.get("extra_sclk_substr"):
            substr = instr["extra_sclk_substr"]
            extra_files = [f for f in files if substr in f and f.endswith(".tsc")]
            fn2 = sorted(extra_files)[-1] if extra_files else None
            if fn2:
                p = _fetch("sclk", "sclk", fn2)
                downloaded.append(p)
            else:
                gs.warning(f"No instrument SCLK matching '{substr}' found.")

    # ── IK ────────────────────────────────────────────────────────────────
    if "ik" in ktypes:
        gs.message("Finding IK …")
        files = _list_dir(f"{base_url}/ik/", timeout)
        ik_hint = (instr["ik"] if instr and instr.get("ik") else sc.get("ik"))
        fn = _latest_file(files, ".ti", ik_hint)
        if fn:
            p = _fetch("ik", "ik", fn)
            downloaded.append(p)
        else:
            gs.warning("No IK (.ti) found.")

    # ── IAK (instrument addendum kernel -- ISIS3's AWS mirror, not NAIF) ──
    if "iak" in ktypes:
        if not instr or not instr.get("iak_prefix"):
            gs.warning("iak requested but no instrument= IAK is known for "
                       f"this spacecraft/instrument (known: "
                       f"{', '.join(sorted(INSTRUMENT.keys()))}).")
        elif sc_name not in AWS_MISSION_DIR:
            gs.warning(f"iak requested but spacecraft={sc_name} has no "
                       "known mission slug on the ISIS3 AWS mirror.")
        else:
            gs.message("Finding IAK (ISIS3 AWS mirror) …")
            aws_prefix = f"usgs_data/{AWS_MISSION_DIR[sc_name]}/kernels/iak/"
            files = _list_s3_dir(aws_prefix, timeout)
            matches = sorted(f for f in files
                              if f.startswith(instr["iak_prefix"])
                              and f.endswith(".ti"))
            fn = matches[-1] if matches else None
            if fn:
                url = f"{AWS_ISIS_DATA}/{aws_prefix}{fn}"
                out_dir = os.path.join(dest, "iak")
                os.makedirs(out_dir, exist_ok=True)
                dst = os.path.join(out_dir, fn)
                if flag_list:
                    gs.message(f"  [iak] {fn}  ({url})")
                else:
                    _download(url, dst, timeout, flag_force)
                downloaded.append(dst)
            else:
                gs.warning(f"No IAK matching '{instr['iak_prefix']}*' found "
                           f"under {AWS_ISIS_DATA}/{aws_prefix}")

    # ── FK ────────────────────────────────────────────────────────────────
    if "fk" in ktypes:
        gs.message("Finding FK …")
        files = _list_dir(f"{base_url}/fk/", timeout)
        fn = _latest_file(files, ".tf", sc.get("fk"))
        if fn:
            p = _fetch("fk", "fk", fn)
            downloaded.append(p)
        else:
            gs.warning("No FK (.tf) found.")

    # ── PCK (mission dir first, fall back to generic_kernels) ─────────────
    if "pck" in ktypes:
        gs.message("Finding PCK …")
        try:
            mission_pck_files = _list_dir(f"{base_url}/pck/", timeout)
        except Exception:
            mission_pck_files = []
        generic_pck_files = None   # lazy fetch

        for pck_hint in (sc.get("pck") or []):
            fn = _latest_file(mission_pck_files, ".tpc", pck_hint)
            if fn:
                p = _fetch("pck", "pck", fn)
                downloaded.append(p)
            else:
                if generic_pck_files is None:
                    generic_pck_files = _list_dir(
                        f"{NAIF_ROOT}/generic_kernels/pck/", timeout)
                fn = _latest_file(generic_pck_files, ".tpc", pck_hint)
                if fn:
                    out_dir = os.path.join(dest, "pck")
                    os.makedirs(out_dir, exist_ok=True)
                    dst = os.path.join(out_dir, fn)
                    if flag_list:
                        gs.message(
                            f"  [pck] {fn}  ({NAIF_ROOT}/generic_kernels/pck/{fn})")
                    else:
                        _download(f"{NAIF_ROOT}/generic_kernels/pck/{fn}",
                                  dst, timeout, flag_force)
                    downloaded.append(dst)

    # ── SPK ───────────────────────────────────────────────────────────────
    if "spk" in ktypes:
        gs.message("Finding SPK …")
        files = _list_dir(f"{base_url}/spk/", timeout)
        fn = _best_spk(files, target_date)
        if fn:
            p = _fetch("spk", "spk", fn)
            downloaded.append(p)
            gs.message(f"  selected: {fn}")
        else:
            gs.warning(f"No SPK covering {target_date} found.")

    # ── CK ────────────────────────────────────────────────────────────────
    if "ck" in ktypes:
        gs.message("Finding CK …")
        files = _list_dir(f"{base_url}/ck/", timeout)
        fn = _best_ck(files, target_date, ck_pref)
        if fn:
            p = _fetch("ck", "ck", fn)
            downloaded.append(p)
            gs.message(f"  selected: {fn}")
        else:
            gs.warning(f"No CK covering {target_date} found (pref={ck_pref}).")

        if instr and instr.get("extra_ck_prefix"):
            prefix = instr["extra_ck_prefix"]
            fn2 = _best_ck(files, target_date, "any", name_prefix=prefix)
            if fn2:
                p = _fetch("ck", "ck", fn2)
                downloaded.append(p)
                gs.message(f"  selected (instrument): {fn2}")
            else:
                gs.warning(f"No instrument CK matching '{prefix}*' covering "
                           f"{target_date} found.")

    # ── Meta-kernel ────────────────────────────────────────────────────────
    if flag_meta and downloaded and not flag_list:
        mk_name = f"{sc_name.lower()}_{target_date.strftime('%Y%j')}.tm"
        mk_path = os.path.join(dest, mk_name)
        _write_metakernel(mk_path, downloaded, sc_name, target_date)

    if not flag_list:
        gs.message(f"Done. {len(downloaded)} kernel(s) in {dest}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
