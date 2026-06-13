#!/usr/bin/env python3
############################################################################
# MODULE:       p.in.rings
# PURPOSE:      Project a raw spacecraft camera image into ring-plane
#               (ring_radius, ring_longitude) coordinates using SPICE.
#               For each output pixel in the current GRASS region — where
#               north/south encode ring_radius [km] and east/west encode
#               ring_longitude [deg] — the module back-projects to the
#               camera pixel using a bilinear ring-geometry model and
#               samples the input DN with bilinear interpolation.
#
#               The ring-plane is the body's equatorial plane (z=0 in the
#               body-fixed frame, e.g. IAU_SATURN for Saturn's rings).
#
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Project a raw camera image to ring-plane (ring_radius/ring_longitude) coordinates using SPICE.
# % keyword: Planetary
# % keyword: Ring Plane Analysis
# % keyword: ring plane
# % keyword: SPICE & Ephemeris
# % keyword: projection
# % keyword: import
# %end

# %option G_OPT_R_INPUT
# % key: input
# % label: Input raw camera image (in pixel/sample coordinates)
# %end

# %option G_OPT_R_OUTPUT
# %end

# %option
# % key: time
# % type: string
# % label: Image mid-time (UTC ISO-8601, e.g. 2004-07-01T03:11:40)
# % required: yes
# %end

# %option
# % key: instrument
# % type: integer
# % label: NAIF instrument ID (e.g. -82360 for Cassini ISS NAC)
# % required: yes
# %end

# %option
# % key: spacecraft
# % type: string
# % label: NAIF spacecraft name or ID (e.g. CASSINI)
# % answer: CASSINI
# % required: no
# %end

# %option
# % key: body
# % type: string
# % label: Central body name (ring plane = equatorial plane of this body)
# % answer: SATURN
# % required: no
# %end

# %option
# % key: frame
# % type: string
# % label: Body-fixed reference frame
# % answer: IAU_SATURN
# % required: no
# %end

# %option
# % key: kernels
# % type: string
# % label: Comma-separated SPICE kernel paths
# % description: If omitted, kernels are loaded from the mapset spice/ directory
# % required: no
# %end

# %option
# % key: grid
# % type: integer
# % label: Geometry sampling grid size (NxN points across image)
# % answer: 9
# % required: no
# %end

# %flag
# % key: n
# % description: Use nearest-neighbour sampling (default: bilinear)
# %end

import os
import sys
import math
import glob
import ctypes
import tempfile

import grass.script as gs
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import p_spice
import p_meta


# ---------------------------------------------------------------------------
# Kernel loading
# ---------------------------------------------------------------------------

def _load_kernels_from_dir(spice_dir):
    if not os.path.isdir(spice_dir):
        gs.fatal(f"No spice/ directory in mapset: {spice_dir}\n"
                 "Run p.spice.find first, or supply kernels= explicitly.")
    for sub in ("lsk", "sclk", "ik", "fk", "pck", "spk", "ck"):
        d = os.path.join(spice_dir, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(glob.glob(os.path.join(d, "*"))):
            if os.path.isfile(f) and not f.endswith(".lbl"):
                p_spice.furnsh(f)
                gs.verbose(f"  loaded {os.path.basename(f)}")


# ---------------------------------------------------------------------------
# Read raster at its native resolution (bypasses current region)
# ---------------------------------------------------------------------------

def _read_raster_native(name):
    """Read a GRASS raster at its native extent (not current region).

    Temporarily switches to the raster's own region, reads all rows via
    r.out.bin, then restores the caller's region.  Returns float64 array
    of shape (nrows, ncols).
    """
    saved = gs.region()
    tmpf = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
    tmpf.close()
    try:
        gs.run_command("g.region", raster=name, quiet=True)
        reg = gs.region()
        nr = int(reg["rows"]); nc = int(reg["cols"])
        gs.run_command("r.out.bin", input=name, output=tmpf.name,
                       bytes=4, flags="f", quiet=True)
        raw = np.fromfile(tmpf.name, dtype=np.float32).reshape(nr, nc)
    finally:
        gs.run_command("g.region",
                       n=saved["n"], s=saved["s"],
                       e=saved["e"], w=saved["w"],
                       nsres=saved["nsres"], ewres=saved["ewres"],
                       quiet=True)
        os.unlink(tmpf.name)
    return raw.astype(np.float64)


# ---------------------------------------------------------------------------
# Write a numpy float64 array as a GRASS raster at the current region
# ---------------------------------------------------------------------------

_DNULL = -9999.0


def _write_raster(name, arr):
    """Write float64 array (nrows, ncols) as FCELL raster via r.in.bin."""
    reg = gs.region()
    tmpf = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
    tmpf.close()
    try:
        flat = np.where(np.isnan(arr), _DNULL, arr).astype(np.float32)
        flat.tofile(tmpf.name)
        gs.run_command("r.in.bin", input=tmpf.name, output=name,
                       bytes=4, flags="f", anull=str(_DNULL),
                       north=reg["n"], south=reg["s"],
                       east=reg["e"], west=reg["w"],
                       rows=reg["rows"], cols=reg["cols"],
                       overwrite=True, quiet=True)
    finally:
        os.unlink(tmpf.name)


# ---------------------------------------------------------------------------
# Ring-geometry bilinear model
# ---------------------------------------------------------------------------

def _build_geometry_model(lib, sc_pos, rot, pscale_s, pscale_l,
                           ns, nl, grid_n):
    """Fit bilinear coefficients for (s,l) → (ring_radius, ring_lon).

    Returns (ar, alon) each a 4-vector:
      r   = ar[0]   + ar[1]*s + ar[2]*l + ar[3]*s*l
      lon = alon[0] + alon[1]*s + alon[2]*l + alon[3]*s*l
    """
    ss = np.linspace(0, ns - 1, grid_n)
    ls = np.linspace(0, nl - 1, grid_n)
    sg, lg = np.meshgrid(ss, ls)
    sg = sg.ravel(); lg = lg.ravel()
    n = len(sg)

    rs = np.zeros(n); lons = np.zeros(n)
    zsc = sc_pos[2]

    for k in range(n):
        s, l = sg[k], lg[k]
        xc = (s - (ns - 1) * 0.5) * pscale_s
        yc = -((l - (nl - 1) * 0.5)) * pscale_l
        zc = 1.0
        mag = math.sqrt(xc*xc + yc*yc + zc*zc)
        xc /= mag; yc /= mag; zc /= mag
        xs = rot[0]*xc + rot[1]*yc + rot[2]*zc
        ys = rot[3]*xc + rot[4]*yc + rot[5]*zc
        zs = rot[6]*xc + rot[7]*yc + rot[8]*zc
        if abs(zs) < 1e-12:
            gs.fatal(f"Look vector parallel to ring plane at pixel ({s:.0f},{l:.0f}).")
        t = -zsc / zs
        if t < 0:
            gs.fatal(f"Ring intercept behind spacecraft at pixel ({s:.0f},{l:.0f}).")
        xi = sc_pos[0] + t * xs
        yi = sc_pos[1] + t * ys
        rs[k]   = math.sqrt(xi*xi + yi*yi)
        lons[k] = math.degrees(math.atan2(yi, xi))

    A = np.column_stack([np.ones(n), sg, lg, sg * lg])
    ar,   _, _, _ = np.linalg.lstsq(A, rs,   rcond=None)
    alon, _, _, _ = np.linalg.lstsq(A, lons, rcond=None)

    res_r   = abs(rs   - A @ ar).max()
    res_lon = abs(lons - A @ alon).max()
    gs.message(f"  Geometry fit residual: r_max={res_r:.4f} km, "
               f"lon_max={res_lon:.6f} deg")
    return ar, alon


# ---------------------------------------------------------------------------
# Bilinear inversion: (ring_radius, ring_lon) → (sample, line)
# ---------------------------------------------------------------------------

def _invert_model(r_out, lon_out, ar, alon, ns, nl, n_iter=4):
    """Invert bilinear model to pixel coordinates; out-of-bounds → NaN."""
    a0, a1, a2, a3 = ar
    b0, b1, b2, b3 = alon

    dr  = r_out   - a0
    dlo = lon_out - b0
    det = a1 * b2 - a2 * b1
    if abs(det) < 1e-30:
        gs.fatal("Degenerate geometry: cannot invert ring-plane model.")
    s = (b2 * dr  - a2 * dlo) / det
    l = (a1 * dlo - b1 * dr)  / det

    for _ in range(n_iter):
        f1 = a0 + a1*s + a2*l + a3*s*l - r_out
        f2 = b0 + b1*s + b2*l + b3*s*l - lon_out
        J00 = a1 + a3 * l;  J01 = a2 + a3 * s
        J10 = b1 + b3 * l;  J11 = b2 + b3 * s
        d = J00 * J11 - J01 * J10
        d = np.where(np.abs(d) < 1e-30, 1e-30, d)
        s -= (J11 * f1 - J01 * f2) / d
        l -= (J00 * f2 - J10 * f1) / d

    mask = (s < -0.5) | (s > ns - 0.5) | (l < -0.5) | (l > nl - 0.5)
    s[mask] = np.nan;  l[mask] = np.nan
    return s, l


# ---------------------------------------------------------------------------
# Bilinear image sampling
# ---------------------------------------------------------------------------

def _sample_bilinear(image, s, l):
    """Sample float64 image[row,col] at float (s,l); NaN where invalid."""
    nl_i, ns_i = image.shape
    out = np.full(s.shape, np.nan, dtype=np.float64)
    valid = ~(np.isnan(s) | np.isnan(l))
    if not np.any(valid):
        return out
    sv = s[valid]; lv = l[valid]
    s0 = np.floor(sv).astype(np.intp); l0 = np.floor(lv).astype(np.intp)
    fs = sv - s0;  fl = lv - l0
    s0c = np.clip(s0,     0, ns_i - 1); s1c = np.clip(s0 + 1, 0, ns_i - 1)
    l0c = np.clip(l0,     0, nl_i - 1); l1c = np.clip(l0 + 1, 0, nl_i - 1)
    v00 = image[l0c, s0c]; v10 = image[l0c, s1c]
    v01 = image[l1c, s0c]; v11 = image[l1c, s1c]
    out[valid] = (v00*(1-fs)*(1-fl) + v10*fs*(1-fl) +
                  v01*(1-fs)*fl     + v11*fs*fl)
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    opt_input  = options["input"]
    opt_output = options["output"]
    opt_time   = options["time"]
    opt_inst   = int(options["instrument"])
    opt_sc     = options["spacecraft"]
    opt_body   = options["body"]
    opt_frame  = options["frame"]
    opt_kerns  = options["kernels"]
    grid_n     = int(options["grid"])
    flag_nn    = flags["n"]

    # ── SPICE setup ────────────────────────────────────────────────────────
    if not p_spice.spice_available():
        gs.fatal("libcspice.so not found. Install planetary-cspice.")
    lib = p_spice.load_library()

    if opt_kerns:
        for k in [x.strip() for x in opt_kerns.split(",")]:
            if not os.path.isfile(k):
                gs.fatal(f"Kernel not found: {k}")
            p_spice.furnsh(k)
    else:
        _load_kernels_from_dir(p_spice.mapset_spice_dir())

    et = p_spice.str2et(opt_time)
    gs.message(f"ET = {et:.3f}")

    # ── Spacecraft position in body-fixed frame ────────────────────────────
    SpiceDouble = ctypes.c_double
    sc_pos = (SpiceDouble * 3)()
    ltime  = SpiceDouble()
    lib.spkpos_c(opt_sc.encode(), SpiceDouble(et),
                 opt_frame.encode(), b"CN+S", opt_body.encode(),
                 sc_pos, ctypes.byref(ltime))
    gs.message(f"  {opt_sc} position: ({sc_pos[0]:.1f}, {sc_pos[1]:.1f}, "
               f"{sc_pos[2]:.1f}) km in {opt_frame}")

    # ── Camera FOV and instrument frame ────────────────────────────────────
    shape  = ctypes.create_string_buffer(64)
    iframe = ctypes.create_string_buffer(64)
    bsight = (SpiceDouble * 3)()
    n_bnd  = ctypes.c_int()
    bounds = (SpiceDouble * 12)()
    lib.getfov_c(ctypes.c_int(opt_inst), ctypes.c_int(4),
                 ctypes.c_int(64), ctypes.c_int(64),
                 shape, iframe, bsight,
                 ctypes.byref(n_bnd), bounds)

    inst_frame = iframe.value.decode()
    gs.message(f"  Instrument frame: {inst_frame}")

    # Rotation from instrument frame to body-fixed
    rot = (SpiceDouble * 9)()
    lib.pxform_c(inst_frame.encode(), opt_frame.encode(),
                 SpiceDouble(et), rot)

    # ── Input image dimensions ─────────────────────────────────────────────
    info = gs.raster_info(opt_input)
    ns = int(info["cols"]); nl = int(info["rows"])
    gs.message(f"  Input: {ns} samples × {nl} lines")

    # Pixel angular scale from FOV boundary vectors
    # bounds[0..2] = first corner: (tan_s, tan_l, 1) approximately
    half_fov_s = abs(math.atan2(abs(bounds[0]), bounds[2]))
    half_fov_l = abs(math.atan2(abs(bounds[1]), bounds[2]))
    pscale_s = half_fov_s / (ns / 2.0)
    pscale_l = half_fov_l / (nl / 2.0)
    gs.message(f"  Pixel scale: {math.degrees(pscale_s)*1e3:.4f} mrad/samp, "
               f"{math.degrees(pscale_l)*1e3:.4f} mrad/line")

    # ── Bilinear geometry model ────────────────────────────────────────────
    gs.message(f"Building ring geometry model ({grid_n}×{grid_n} grid) …")
    ar, alon = _build_geometry_model(lib, sc_pos, rot, pscale_s, pscale_l,
                                      ns, nl, grid_n)

    # ── Read input image at native resolution ──────────────────────────────
    gs.message("Reading input image …")
    raw = _read_raster_native(opt_input)

    # ── Output pixel ring coordinates ──────────────────────────────────────
    reg = gs.region()
    out_rows = int(reg["rows"]); out_cols = int(reg["cols"])
    gs.message(f"Output region: {out_cols} × {out_rows}  "
               f"r=[{reg['s']:.1f}, {reg['n']:.1f}] km  "
               f"lon=[{reg['w']:.4f}, {reg['e']:.4f}] deg")

    col_g, row_g = np.meshgrid(np.arange(out_cols), np.arange(out_rows))
    r_out   = reg["n"] - (row_g + 0.5) * reg["nsres"]
    lon_out = reg["w"] + (col_g + 0.5) * reg["ewres"]

    # ── Invert geometry ────────────────────────────────────────────────────
    gs.message("Inverting ring geometry …")
    s_in, l_in = _invert_model(r_out.ravel(), lon_out.ravel(),
                                ar, alon, ns, nl)
    s_in = s_in.reshape(out_rows, out_cols)
    l_in = l_in.reshape(out_rows, out_cols)

    coverage = np.sum(~np.isnan(s_in))
    gs.message(f"  Valid output pixels: {coverage} / {out_rows*out_cols} "
               f"({100*coverage/(out_rows*out_cols):.1f}%)")

    # ── Sample input image ─────────────────────────────────────────────────
    gs.message("Sampling …")
    if flag_nn:
        sc = np.round(s_in).astype(np.intp)
        lc = np.round(l_in).astype(np.intp)
        valid = ~np.isnan(s_in)
        out_dn = np.full((out_rows, out_cols), np.nan)
        scv = np.clip(sc[valid], 0, ns-1)
        lcv = np.clip(lc[valid], 0, nl-1)
        out_dn[valid] = raw[lcv, scv]
    else:
        out_dn = _sample_bilinear(raw, s_in, l_in)

    # ── Write output raster ────────────────────────────────────────────────
    gs.message(f"Writing '{opt_output}' …")
    _write_raster(opt_output, out_dn)
    gs.run_command("r.colors", map=opt_output, color="grey", quiet=True)

    kernel_list = [k.strip() for k in opt_kerns.split(",")] if opt_kerns else None
    p_meta.write_planetary_metadata(
        opt_output,
        module="p.in.rings",
        command=" ".join(sys.argv),
        data_type="rings",
        sensor=str(opt_inst),
        mission=opt_sc,
        body=opt_body,
        radiometric_quantity="raw_dn",
        radiometric_units="DN",
        acquisition_datetime=opt_time,
        spice_kernels=kernel_list,
    )

    gs.message(f"Done: {opt_output}")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
