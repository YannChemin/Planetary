#!/usr/bin/env python3
"""
MODULE:    p.stereo
AUTHOR:    Yann Chemin <dr.yann.chemin@gmail.com>
PURPOSE:   Stereo DEM production via NASA Ames Stereo Pipeline (ASP).
           Wraps ASP's stereo + point2dem pipeline with planetary body
           parameters derived from the bodies/ JSON library. Imports the
           resulting DEM into the current GRASS mapset.
LICENSE:   The Unlicense (https://unlicense.org)
"""

# %module
# % description: Stereo DEM production from two overlapping planetary images via NASA ASP stereo + point2dem.
# % keyword: Planetary
# % keyword: Topography
# % keyword: stereo
# % keyword: DEM
# % keyword: ASP
# % keyword: shape-from-stereo
# %end

# %option
# % key: left
# % type: string
# % required: yes
# % multiple: no
# % label: Left input image (ISIS .cub, PDS .img, or GeoTIFF)
# % description: First (left/reference) overlapping image. Must be SPICE-initialised (.cub) for ISIS stereo mode.
# %end

# %option
# % key: right
# % type: string
# % required: yes
# % multiple: no
# % label: Right input image (ISIS .cub, PDS .img, or GeoTIFF)
# % description: Second (right/match) overlapping image, same sensor and orbit pair.
# %end

# %option G_OPT_R_OUTPUT
# % label: Output GRASS DEM raster name
# %end

# %option
# % key: body
# % type: string
# % required: yes
# % multiple: no
# % label: Planetary body
# % options: mars,moon,mercury,venus,titan,europa,enceladus,ceres
# % description: Target body — used to set semi-major and semi-minor axes for point2dem.
# %end

# %option
# % key: workdir
# % type: string
# % required: no
# % multiple: no
# % label: Working directory for ASP intermediate files
# % description: ASP writes large intermediate files (disparity, point cloud). Defaults to $HOME/RSDATA/<body>/stereo/<basename>/.
# %end

# %option
# % key: alignment
# % type: string
# % required: no
# % multiple: no
# % answer: affineepipolar
# % options: affineepipolar,homography,local_epipolar,epipolar,none
# % label: Stereo alignment method (--alignment-method)
# % description: affineepipolar works well for orbital nadir/oblique pairs; local_epipolar for very oblique or wide-baseline pairs.
# %end

# %option
# % key: algorithm
# % type: string
# % required: no
# % multiple: no
# % answer: asp_mgm
# % options: asp_bm,asp_sgm,asp_mgm,asp_final_mgm,msmw,msmw2,opencv_bm,opencv_sgbm
# % label: Stereo correlation algorithm (--stereo-algorithm)
# % description: asp_mgm (default) is accurate and memory-efficient for orbital imagery. asp_bm is fastest but less accurate. msmw2 handles large radiometric differences.
# %end

# %option
# % key: spacing
# % type: double
# % required: no
# % multiple: no
# % label: Output DEM posting/resolution in metres (--dem-spacing)
# % description: Defaults to 0 (point2dem auto-selects from point cloud density).
# %end

# %option
# % key: stereo_opts
# % type: string
# % required: no
# % multiple: no
# % label: Extra options passed verbatim to stereo
# % description: E.g. "--corr-kernel 25 25 --subpixel-mode 2 --threads 8"
# %end

# %option
# % key: point2dem_opts
# % type: string
# % required: no
# % multiple: no
# % label: Extra options passed verbatim to point2dem
# % description: E.g. "--median-filter-params 3 3 --erode-length 1"
# %end

# %flag
# % key: b
# % label: Run bundle_adjust before stereo
# % description: Improves relative camera alignment. Adds significant runtime. Requires bundle_adjust in PATH (part of ASP).
# %end

# %flag
# % key: k
# % label: Keep intermediate ASP files after import
# % description: By default the stereo output directory is removed after the DEM is imported into GRASS.
# %end

# %flag
# % key: o
# % label: Orthorectify both images after DEM production (mapproject)
# % description: Runs ASP mapproject to project each image onto the DEM. Imports orthorectified results as named_output_left and named_output_right.
# %end

import os
import sys
import json
import shutil
import subprocess
import tempfile
import tarfile
import urllib.request

import grass.script as gs

# ------------------------------------------------------------------ #
# Body data                                                            #
# ------------------------------------------------------------------ #

_BODIES_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "bodies")


def _load_body(body):
    path = os.path.join(_BODIES_DIR, f"{body}.json")
    if not os.path.isfile(path):
        gs.fatal(f"Body '{body}' not found in bodies/ library (checked {path}).")
    with open(path) as f:
        return json.load(f)


# ------------------------------------------------------------------ #
# ASP detection and auto-install                                       #
# ------------------------------------------------------------------ #

# Source tree cloned by the Makefile (or by the user)
_ASP_SRC = os.path.expanduser("~/dev/StereoPipeline")

# Pre-compiled binary tarball extracted here (separate from source tree)
_ASP_BIN_INSTALL = os.path.expanduser("~/dev/StereoPipeline-bin")

_ASP_SEARCH = [
    os.path.join(_ASP_BIN_INSTALL, "bin"),        # binary tarball install
    os.path.expanduser("~/.pixi/bin"),             # pixi global install
    "/usr/local/asp/bin",
    "/opt/asp/bin",
    os.path.expanduser("~/asp/bin"),
    os.path.expanduser("~/StereoPipeline/bin"),
    os.path.expanduser("~/bin"),
]

# GitHub releases API for StereoPipeline
_ASP_API = "https://api.github.com/repos/NeoGeographyToolkit/StereoPipeline/releases/latest"


def _find_asp():
    """Return the directory containing ASP binaries, or None."""
    for tool in ("stereo", "point2dem"):
        p = shutil.which(tool)
        if p:
            return os.path.dirname(p)
    for d in _ASP_SEARCH:
        if os.path.isfile(os.path.join(d, "stereo")):
            return d
    return None


def _latest_asp_linux_url():
    """Query GitHub releases API for the latest Linux x86_64 binary tarball URL."""
    try:
        req = urllib.request.Request(
            _ASP_API,
            headers={"Accept": "application/vnd.github+json", "User-Agent": "p.stereo/GRASS"},
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            release = json.loads(resp.read())
        for asset in release.get("assets", []):
            name = asset["name"]
            if "x86_64" in name and "Linux" in name and (
                name.endswith(".tar.bz2") or name.endswith(".tar.gz")
            ):
                return asset["browser_download_url"], release["tag_name"]
        # fallback: any Linux tarball
        for asset in release.get("assets", []):
            name = asset["name"]
            if "Linux" in name and (name.endswith(".tar.bz2") or name.endswith(".tar.gz")):
                return asset["browser_download_url"], release["tag_name"]
    except Exception as e:
        gs.warning(f"Could not query GitHub releases API: {e}")
    return None, None


def _try_pixi_install():
    """Try to install ASP via pixi global from the nasa-ames-stereo-pipeline channel."""
    pixi = shutil.which("pixi")
    if pixi is None:
        return False
    gs.message("Trying: pixi global install asp (nasa-ames-stereo-pipeline channel) ...")
    ret = subprocess.run(
        [pixi, "global", "install",
         "--channel", "nasa-ames-stereo-pipeline",
         "--channel", "conda-forge",
         "asp"],
        check=False,
    )
    if ret.returncode == 0:
        asp_dir = _find_asp()
        if asp_dir:
            gs.message(f"ASP installed via pixi at: {asp_dir}")
            return True
    gs.warning("pixi install asp failed — falling back to binary tarball download.")
    return False


def _auto_install_asp():
    """Install ASP: try pixi first, then download pre-compiled binary tarball."""
    gs.message("ASP not found. Attempting automatic installation ...")

    # 1) Try pixi global install
    if _try_pixi_install():
        return _find_asp()

    # 2) Download binary tarball to ~/dev/StereoPipeline-bin/
    gs.message("Downloading pre-compiled ASP binary tarball from GitHub ...")
    url, tag = _latest_asp_linux_url()
    if url is None:
        gs.warning(
            "Could not determine latest ASP release URL.\n"
            "Download manually from https://github.com/NeoGeographyToolkit/StereoPipeline/releases\n"
            f"and extract to {_ASP_BIN_INSTALL}/"
        )
        return None

    gs.message(f"ASP {tag}  URL: {url}")
    tarball = os.path.join(tempfile.gettempdir(), os.path.basename(url))
    try:
        def _progress(block_num, block_size, total_size):
            if total_size > 0:
                pct = int(block_num * block_size * 100 / total_size)
                sys.stderr.write(f"\r  Downloading: {min(pct, 100):3d}%  ")
        urllib.request.urlretrieve(url, tarball, reporthook=_progress)
        sys.stderr.write("\n")
    except Exception as e:
        gs.fatal(f"Download failed: {e}")

    gs.message(f"Extracting to {_ASP_BIN_INSTALL} ...")
    os.makedirs(_ASP_BIN_INSTALL, exist_ok=True)
    try:
        with tarfile.open(tarball) as tf:
            members = tf.getmembers()
            # Strip top-level dir (e.g. StereoPipeline-3.7.0-...-Linux/)
            top = members[0].name.split("/")[0] if members else ""
            for m in members:
                rel = m.name[len(top):].lstrip("/") if m.name.startswith(top) else m.name
                if not rel:
                    continue
                m.name = rel
                tf.extract(m, _ASP_BIN_INSTALL)
    except Exception as e:
        gs.fatal(f"Extraction failed: {e}")
    finally:
        if os.path.isfile(tarball):
            os.unlink(tarball)

    bin_dir = os.path.join(_ASP_BIN_INSTALL, "bin")
    if os.path.isfile(os.path.join(bin_dir, "stereo")):
        gs.message(f"ASP binary installed at {_ASP_BIN_INSTALL}")
        gs.message(f"Add to ~/.bashrc:  export PATH=\"$PATH:{bin_dir}\"")
        return bin_dir
    gs.warning(
        f"Extraction complete but 'stereo' not found in {bin_dir}.\n"
        f"Check {_ASP_BIN_INSTALL}/ manually."
    )
    return None


def _require_asp():
    """Return ASP bin directory; auto-install if missing."""
    asp_dir = _find_asp()
    if asp_dir:
        return asp_dir

    src_present = os.path.isdir(_ASP_SRC)
    gs.warning(
        "ASP binaries not found in PATH or standard locations.\n"
        f"Source tree: {_ASP_SRC}  ({'present' if src_present else 'not present'}).\n"
        "Will attempt automatic installation ..."
    )
    asp_dir = _auto_install_asp()
    if asp_dir is None:
        gs.fatal(
            "Could not install ASP automatically.\n"
            "Options:\n"
            "  1) Download binary:  https://github.com/NeoGeographyToolkit/StereoPipeline/releases\n"
            f"     Extract to {_ASP_BIN_INSTALL}/ and ensure bin/ is in your PATH.\n"
            "  2) Build from source:\n"
            f"     cd {_ASP_SRC} && cat INSTALLGUIDE.rst\n"
            "  3) pixi global install asp  (requires nasa-ames-stereo-pipeline channel)"
        )
    return asp_dir


def _asp_cmd(asp_dir, tool):
    return os.path.join(asp_dir, tool) if asp_dir else tool


# ------------------------------------------------------------------ #
# Helpers                                                              #
# ------------------------------------------------------------------ #

def _run(args, label):
    gs.message(f"Running: {' '.join(str(a) for a in args)}")
    ret = subprocess.run(args, check=False)
    if ret.returncode != 0:
        gs.fatal(f"{label} failed with exit code {ret.returncode}.")


def _shlex_split(s):
    import shlex
    return shlex.split(s) if s else []


# ------------------------------------------------------------------ #
# main                                                                 #
# ------------------------------------------------------------------ #

def main():
    opt_left        = options["left"]
    opt_right       = options["right"]
    opt_output      = options["output"]
    opt_body        = options["body"]
    opt_workdir     = options["workdir"]
    opt_alignment   = options["alignment"]
    opt_algorithm   = options["algorithm"]
    opt_spacing     = options["spacing"]
    opt_stereo_opts = options["stereo_opts"]
    opt_p2d_opts    = options["point2dem_opts"]
    flag_b          = flags["b"]
    flag_k          = flags["k"]
    flag_o          = flags["o"]

    # ---- Validate inputs ----
    for path, label in ((opt_left, "left"), (opt_right, "right")):
        if not os.path.isfile(path):
            gs.fatal(f"Input file not found: {path} (option {label}=)")

    # ---- Load body parameters ----
    body = _load_body(opt_body)
    semi_major_m = body["semi_major_axis_m"]
    semi_minor_m = body["semi_minor_axis_m"]
    gs.message(
        f"Body: {body['name']}  a={semi_major_m} m  b={semi_minor_m} m"
    )

    # ---- Find ASP ----
    asp_dir = _require_asp()
    gs.message(f"ASP found at: {asp_dir}")

    # ---- Working directory ----
    basename = os.path.splitext(os.path.basename(opt_left))[0]
    if opt_workdir:
        workdir = opt_workdir
    else:
        rsdata = os.environ.get("HOME", os.path.expanduser("~"))
        workdir = os.path.join(rsdata, "RSDATA", body["name"], "stereo", basename)

    stereo_prefix = os.path.join(workdir, "stereo", "run")
    ba_prefix     = os.path.join(workdir, "ba", "run")
    dem_prefix    = os.path.join(workdir, "dem", opt_output)

    os.makedirs(os.path.dirname(stereo_prefix), exist_ok=True)
    os.makedirs(os.path.dirname(ba_prefix),     exist_ok=True)
    os.makedirs(os.path.dirname(dem_prefix),    exist_ok=True)

    # ---- Bundle adjustment (optional) ----
    if flag_b:
        gs.message("Step 1/3: bundle_adjust")
        ba_cmd = [
            _asp_cmd(asp_dir, "bundle_adjust"),
            opt_left, opt_right,
            "-o", ba_prefix,
        ]
        _run(ba_cmd, "bundle_adjust")
        # pass BA prefix to stereo
        ba_flag = ["--bundle-adjust-prefix", ba_prefix]
    else:
        ba_flag = []
        gs.message("Step 1/3: bundle_adjust  [skipped, use -b to enable]")

    # ---- Stereo correlation ----
    gs.message("Step 2/3: stereo")
    stereo_cmd = (
        [_asp_cmd(asp_dir, "stereo")]
        + [opt_left, opt_right]
        + [stereo_prefix]
        + ["--alignment-method", opt_alignment]
        + ["--stereo-algorithm", opt_algorithm]
        + ba_flag
        + _shlex_split(opt_stereo_opts)
    )
    _run(stereo_cmd, "stereo")

    # ---- Point cloud to DEM ----
    gs.message("Step 3/3: point2dem")
    pc_file = stereo_prefix + "-PC.tif"
    if not os.path.isfile(pc_file):
        gs.fatal(
            f"Stereo point cloud not found: {pc_file}\n"
            "stereo may have produced no output. Check the stereo log."
        )

    p2d_cmd = (
        [_asp_cmd(asp_dir, "point2dem"), pc_file]
        + ["--semi-major-axis", str(semi_major_m)]
        + ["--semi-minor-axis", str(semi_minor_m)]
        + ["--nodata-value", "-9999"]
        + (["--dem-spacing", opt_spacing] if opt_spacing else [])
        + ["-o", dem_prefix]
        + _shlex_split(opt_p2d_opts)
    )
    _run(p2d_cmd, "point2dem")

    dem_tif = dem_prefix + "-DEM.tif"
    if not os.path.isfile(dem_tif):
        gs.fatal(f"point2dem output not found: {dem_tif}")

    # ---- Import DEM into GRASS ----
    gs.message(f"Importing DEM into GRASS: {opt_output}")
    gs.run_command(
        "r.import",
        input=dem_tif,
        output=opt_output,
        resample="bilinear",
        quiet=True,
    )
    gs.run_command("r.colors", map=opt_output, color="srtm", quiet=True)

    # ---- Optional: mapproject both images onto the DEM ----
    if flag_o:
        for side, img_path in (("left", opt_left), ("right", opt_right)):
            out_name = f"{opt_output}_{side}"
            ortho_prefix = os.path.join(workdir, f"ortho_{side}", "run")
            os.makedirs(os.path.dirname(ortho_prefix), exist_ok=True)
            ortho_tif = ortho_prefix + "-DRG.tif"

            gs.message(f"Orthorectifying {side} image → {out_name}")
            mp_cmd = [
                _asp_cmd(asp_dir, "mapproject"),
                dem_tif,
                img_path,
                ortho_tif,
                "--t_srs",
                f"+proj=longlat +a={semi_major_m} +b={semi_minor_m} +no_defs",
            ]
            _run(mp_cmd, f"mapproject ({side})")

            if os.path.isfile(ortho_tif):
                gs.run_command(
                    "r.import",
                    input=ortho_tif,
                    output=out_name,
                    resample="bilinear",
                    quiet=True,
                )
                gs.message(f"  Imported as <{out_name}>")

    # ---- Cleanup ----
    if not flag_k:
        gs.message(f"Removing intermediate files in {workdir}")
        shutil.rmtree(workdir, ignore_errors=True)
    else:
        gs.message(f"Intermediate files kept in {workdir}")

    gs.message(
        f"Done. DEM imported as <{opt_output}>.\n"
        f"Tip: r.info map={opt_output}  and  d.shade shade={opt_output} "
        f"color={opt_output}"
    )


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
