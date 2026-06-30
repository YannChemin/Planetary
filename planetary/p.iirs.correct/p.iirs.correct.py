#!/usr/bin/env python3
"""
MODULE:    p.iirs.correct
AUTHOR:    Yann Chemin
PURPOSE:   Thermal emission correction for Chandrayaan-2 IIRS Level-2 radiance.

           Ports the Verma-Chauhan-Chauhan (2022) algorithm from their
           CH2IIRS QGIS plugin to GRASS GIS (Verma, P.A., Chauhan, M., &
           Chauhan, P., 2022. Lunar surface temperature estimation and thermal
           emission correction using Chandrayaan-2 imaging infrared
           spectrometer data for H2O & OH detection using 3 µm hydration
           feature. Icarus, 383, 115075).

           Inputs:
             - IIRS L2 radiance imagery group (256 bands, imported via
               r.in.gdal from ISSDC ENVI files)
             - Solar irradiance spectrum (2-column ASCII: wavelength_nm flux)
               Default: bundled IIRS solar reference spectrum

           Outputs:
             - Thermally-corrected reflectance imagery group (bands 800-5000 nm)
             - Surface temperature raster

LICENSE:   The Unlicense (https://unlicense.org) - public domain
"""

# %module
# % description: Thermal emission correction for Chandrayaan-2 IIRS Level-2 radiance (Verma et al. 2022).
# % keyword: imagery
# % keyword: hyperspectral
# % keyword: thermal correction
# % keyword: Chandrayaan-2
# % keyword: IIRS
# % keyword: Moon
# % keyword: planetary
# %end

# %option G_OPT_I_GROUP
# % key: input
# % required: yes
# % label: IIRS L2 radiance imagery group (256 bands from ISSDC ENVI file)
# % description: All 256 IIRS bands must be present in group order (band 1 = 712 nm). Import via r.in.gdal from ISSDC ENVI cube and add bands to imagery group with i.group.
# %end

# %option G_OPT_R_OUTPUT
# % key: output
# % required: yes
# % label: Output raster base name
# % description: Produces <output>.b001..<output>.bNNN thermally-corrected reflectance maps (800-5000 nm subset) plus <output>.temp temperature raster.
# %end

# %option
# % key: solar_flux
# % type: string
# % required: no
# % label: Solar irradiance spectrum file (optional)
# % description: Two-column ASCII file: wavelength_nm, solar_irradiance. Must cover all 256 IIRS bands (712-5010 nm). If omitted, the bundled IIRS solar reference spectrum is used.
# %end

# %option
# % key: emissivity
# % type: double
# % required: no
# % answer: 0.95
# % label: Surface emissivity (0-1)
# % description: Assumed constant emissivity for the Planck thermal component. Default 0.95 follows Verma et al. (2022) for the lunar regolith.
# %end

# %option
# % key: wave_lo
# % type: double
# % required: no
# % answer: 4500
# % label: Lower wavelength bound (nm) for temperature retrieval window
# % description: Temperature is estimated by Planck-function inversion across [wave_lo, wave_hi]. Default 4500-4874 nm is the thermal region used in Verma et al. (2022).
# %end

# %option
# % key: wave_hi
# % type: double
# % required: no
# % answer: 4874
# % label: Upper wavelength bound (nm) for temperature retrieval window
# %end

# %flag
# % key: g
# % description: Create output imagery group (adds all reflectance bands to the named output group via i.group)
# %end

import os
import sys
import tempfile

import grass.script as gs

# ── Physical constants ─────────────────────────────────────────────────────
_C = 3e8        # speed of light (m/s)
_H = 6.626e-34  # Planck constant (J·s)
_K = 1.38e-23   # Boltzmann constant (J/K)

# ── Per-band correction coefficients (Verma et al. 2022, Table S1) ────────
# 240 values applied to the 247-band (7:-2) working subset trimmed to the
# 800-5000 nm output window.  The QGIS plugin applies them AFTER the
# reflectance is computed across the full 247-band range, then writes only
# the 800-5000 nm window.  These coefficients correspond exactly to that
# output window (bands whose wavelength falls in 800-5000 nm after trimming).
_IIRS_COEFF = [
    1.007951631, 1.003136498, 1.012137177, 0.983221552, 0.982301846,
    0.980570808, 0.974865433, 0.97640194,  0.992019454, 1.007100398,
    1.004167929, 0.986444903, 1.005016461, 1.013553818, 1.009490688,
    1.003308071, 1.011969658, 1.0141375,   1.006193945, 1.015513554,
    1.020218579, 1.016652203, 1.023330972, 1.020211072, 1.015149832,
    1.00860599,  1.000767149, 0.994761146, 1.067891991, 1.044841918,
    0.972851837, 0.9762951,   0.979046208, 0.97102683,  0.982455709,
    0.987001059, 0.988436954, 0.979160136, 0.9986847,   0.997752521,
    0.998447812, 1.016854934, 0.997900542, 0.983831379, 0.969376425,
    0.979670198, 0.973878228, 0.996289037, 1.014184498, 1.01477613,
    1.001481496, 0.975778394, 0.997684295, 0.984763646, 0.983395789,
    0.98823384,  0.988012215, 0.952374697, 0.937581808, 1.013480796,
    1.014063354, 1.022099753, 1.03204087,  1.035484491, 1.041175268,
    1.036634478, 1.034566162, 1.03809732,  1.046558655, 1.021576737,
    1.028588801, 1.007904219, 0.972531419, 0.956629793, 0.955287872,
    0.979925337, 1.002466257, 1.014420813, 1.008463966, 1.00003099,
    1.008471522, 0.99282371,  0.99980302,  0.987547313, 0.984068212,
    0.992388063, 0.988139868, 1.012650312, 1.052139241, 1.012472306,
    0.965437548, 0.939243701, 0.967441753, 0.963544372, 0.990952989,
    0.973117569, 1.013248635, 1.020203633, 1.029157186, 1.020485688,
    1.020579985, 1.006238104, 1.014835128, 1.006275303, 1.010898129,
    1.00045314,  1.008028897, 1.012421421, 1.027533403, 1.031189861,
    1.029572772, 0.998242,    0.988472694, 0.964883574, 0.965888213,
    0.949632716, 0.959468989, 0.953967103, 0.97185828,  0.975099523,
    0.99858193,  0.998573727, 1.021937907, 1.019142949, 1.038073792,
    1.031876174, 1.041953094, 1.029414494, 1.031461328, 1.008333598,
    1.0158236,   0.997626672, 0.997772942, 0.951297807, 0.98848544,
    0.983055255, 0.973287938, 1.020004362, 1.007683124, 1.002357289,
    1.009943767, 0.967795252, 0.999227534, 0.97305723,  0.984516543,
    0.996874775, 1.05340332,  1.037099582, 1.015145219, 1.02252679,
    0.987330432, 0.896931034, 0.854160633, 1.141899078, 1.108682789,
    1.023030659, 0.939513645, 0.914993685, 0.994857247, 1.264567511,
    0.759581027, 1.008047196, 1.012150301, 0.962843991, 1.019548525,
    0.989332932, 1.049050719, 0.980318607, 1.018721793, 0.965286873,
    1.027176253, 0.963709485, 1.010598964, 0.971110269, 1.076940686,
    1.020255436, 0.929940419, 0.996612177, 0.958811048, 1.059205186,
    1.002448358, 0.973559634, 1.002937935, 1.013683258, 1.0474072,
    1.000925241, 0.985500261, 0.982660401, 0.938587583, 1.046900212,
    0.952743318, 1.026649622, 1.115011575, 0.887761742, 1.005459078,
    0.95483364,  0.967275555, 1.13852458,  0.986343909, 0.965651948,
    1.101161236, 0.988116073, 0.853138055, 0.974375472, 1.177442497,
    0.927739407, 0.932486397, 0.962300997, 0.985181241, 0.829206405,
    1.493934047, 1.059590538, 0.71322689,  0.875132631, 1.121072252,
    0.938541303, 1.139668914, 0.897165185, 1.117762208, 0.961511703,
    0.528956345, 1.711570255, 1.20749058,  1.024243553, 0.713165588,
    1.06875679,  0.570841199, 0.932178556, 2.546499369, 2.664596812,
    1.224978705, 0.567434034, 0.726464718, 0.745085856, 1.21801428,
    1.140071493, 0.828417838, 1.063662997, 0.953993832, 1.084346492,
    0.826805177, 1.166156321, 0.873581953, 0.906603941, 1.403382254,
    0.898905495, 1.006518779,
]

# ── Bundled IIRS solar irradiance (wavelength_nm, W/m²/nm) × 256 bands ────
# Source: "Solar flux.txt" from CH2IIRS QGIS plugin (Verma et al. 2022).
# Columns: wavelength_nm, solar_irradiance_W_m2_nm
_SOLAR_FLUX_NM = [
     712.3339889,  729.186351,   746.0387131,  762.8910752,  779.7434373,
     796.5957994,  813.4481615,  830.3005236,  847.1528857,  864.0052478,
     880.8576099,  897.709972,   914.5623341,  931.4146962,  948.2670583,
     965.1194204,  981.9717825,  998.8241446, 1015.676507,  1032.528869,
    1049.381231,  1066.233593,  1083.085955,  1099.938317,  1116.790679,
    1133.643041,  1150.495404,  1167.347766,  1184.200128,  1201.05249,
    1217.904852,  1234.757214,  1251.609576,  1268.461938,  1285.3143,
    1302.166662,  1319.019025,  1335.871387,  1352.723749,  1369.576111,
    1386.428473,  1403.280835,  1420.133197,  1436.985559,  1453.837921,
    1470.690284,  1487.542646,  1504.395008,  1521.24737,   1538.099732,
    1554.952094,  1571.804456,  1588.656818,  1605.50918,   1622.361542,
    1639.213905,  1656.066267,  1672.918629,  1689.770991,  1706.623353,
    1723.475715,  1740.328077,  1757.180439,  1774.032801,  1790.885164,
    1807.737526,  1824.589888,  1841.44225,   1858.294612,  1875.146974,
    1891.999336,  1908.851698,  1925.70406,   1942.556423,  1959.408785,
    1976.261147,  1993.113509,  2009.965871,  2026.818233,  2043.670595,
    2060.522957,  2077.375319,  2094.227682,  2111.080044,  2127.932406,
    2144.784768,  2161.63713,   2178.489492,  2195.341854,  2212.194216,
    2229.046578,  2245.898941,  2262.751303,  2279.603665,  2296.456027,
    2313.308389,  2330.160751,  2347.013113,  2363.865475,  2380.717837,
    2397.570199,  2414.422562,  2431.274924,  2448.127286,  2464.979648,
    2481.83201,   2498.684372,  2515.536734,  2532.389096,  2549.241459,
    2566.093821,  2582.946183,  2599.798545,  2616.650907,  2633.503269,
    2650.355631,  2667.207993,  2684.060355,  2700.912718,  2717.76508,
    2734.617442,  2751.469804,  2768.322166,  2785.174528,  2802.02689,
    2818.879252,  2835.731614,  2852.583977,  2869.436339,  2886.288701,
    2903.141063,  2919.993425,  2936.845787,  2953.698149,  2970.550511,
    2987.402873,  3004.255236,  3021.107598,  3037.95996,   3054.812322,
    3071.664684,  3088.517046,  3105.369408,  3122.22177,   3139.074132,
    3155.926495,  3172.778857,  3189.631219,  3206.483581,  3223.335943,
    3240.188305,  3257.040667,  3273.893029,  3290.745391,  3307.597754,
    3324.450116,  3341.302478,  3358.15484,   3375.007202,  3391.859564,
    3408.711926,  3425.564288,  3442.41665,   3459.269013,  3476.121375,
    3492.973737,  3509.826099,  3526.678461,  3543.530823,  3560.383185,
    3577.235547,  3594.087909,  3610.940272,  3627.792634,  3644.644996,
    3661.497358,  3678.34972,   3695.202082,  3712.054444,  3728.906806,
    3745.759168,  3762.611531,  3779.463893,  3796.316255,  3813.168617,
    3830.020979,  3846.873341,  3863.725703,  3880.578065,  3897.430427,
    3914.28279,   3931.135152,  3947.987514,  3964.839876,  3981.692238,
    3998.5446,    4015.396962,  4032.249324,  4049.101686,  4065.954049,
    4082.806411,  4099.658773,  4116.511135,  4133.363497,  4150.215859,
    4167.068221,  4183.920583,  4200.772945,  4217.625308,  4234.47767,
    4251.330032,  4268.182394,  4285.034756,  4301.887118,  4318.73948,
    4335.591842,  4352.444204,  4369.296567,  4386.148929,  4403.001291,
    4419.853653,  4436.706015,  4453.558377,  4470.410739,  4487.263101,
    4504.115463,  4520.967826,  4537.820188,  4554.67255,   4571.524912,
    4588.377274,  4605.229636,  4622.081998,  4638.93436,   4655.786722,
    4672.639085,  4689.491447,  4706.343809,  4723.196171,  4740.048533,
    4756.900895,  4773.753257,  4790.605619,  4807.457981,  4824.310344,
    4841.162706,  4858.015068,  4874.86743,   4891.719792,  4908.572154,
    4925.424516,  4942.276878,  4959.12924,   4975.981602,  4992.833964,
    5009.686326,
]
_SOLAR_FLUX_W = [
    136.1259307, 129.8781929, 125.1457188, 120.4566749, 115.2187742,
    110.7989129, 105.971862,  102.2853476,  98.83159112,  95.00990644,
     91.61753124,  88.44143028,  86.1143538,   83.5396,      81.28248416,
     79.21023716,  77.43090636,  75.32296698,  73.50380148,  71.47047256,
     69.73060032,  67.57095044,  65.30497588,  63.67267008,  61.89534184,
     59.95018936,  57.72064968,  55.91895444,  54.20040348,  52.61451404,
     50.58038484,  48.95887916,  47.94494904,  46.21609908,  44.26614492,
     43.31153532,  42.43296588,  40.60571668,  38.73736908,  37.88892228,
     36.83226804,  35.51082684,  34.41208584,  33.89162348,  32.5869484,
     31.49793696,  30.75038572,  29.9843876,   29.56484444,  28.55791544,
     27.71424048,  27.11263332,  26.29993744,  25.47706288,  24.84165124,
     24.22637008,  23.65093044,  22.84688556,  22.23116608,  21.78826164,
     21.45720244,  20.73987944,  20.21234904,  19.55990192,  19.13779276,
     18.7476592,   18.32501236,  17.89455088,  17.4804024,   17.11882672,
     16.6893572,   16.34481132,  15.98462388,  15.68175088,  15.30399828,
     14.98440848,  14.67018696,  14.38128864,  14.08547376,  13.79706452,
     13.4898356,   13.23694164,  12.99167844,  12.6988468,   12.42741764,
     12.1636892,   11.91869928,  11.67702752,  11.43601596,  11.18977408,
     10.97453276,  10.8089464,   10.57049764,  10.28791836,  10.08040048,
      9.89699836,   9.71208836,   9.47499044,   9.29447376,   9.14139776,
      8.89706428,   8.73543168,   8.58869608,   8.42483896,   8.28437796,
      8.09756448,   7.92716308,   7.77680296,   7.66079824,   7.51195188,
      7.35474796,   7.22375028,   7.08416056,   6.97082652,   6.86122416,
      6.72984064,   6.6037568,    6.46906488,   6.34991496,   6.24254872,
      6.15087512,   6.0316116,    5.93286932,   5.80736588,   5.68988796,
      5.60193012,   5.51017476,   5.38785452,   5.27701612,   5.19742552,
      5.11220928,   5.02036748,   4.92568516,   4.83696264,   4.74939408,
      4.67344148,   4.59195688,   4.51265476,   4.4330628,    4.36040396,
      4.28990184,   4.21949668,   4.14987032,   4.06498128,   3.99988704,
      3.93527624,   3.8726988,    3.81028676,   3.74908956,   3.69049844,
      3.63380592,   3.57847844,   3.52411232,   3.47092176,   3.41884244,
      3.36757496,   3.31744948,   3.26870296,   3.22052476,   3.17340044,
      3.12826076,   3.0842936,    3.04044244,   2.99678948,   2.95408884,
      2.91255264,   2.87143028,   2.83078292,   2.79104548,   2.75207564,
      2.71385,      2.67617616,   2.63960236,   2.60361692,   2.56814876,
      2.53333648,   2.49902184,   2.46536096,   2.43208644,   2.39984444,
      2.36813196,   2.33686572,   2.30651696,   2.27657104,   2.24730256,
      2.218637,     2.18968148,   2.16148384,   2.13353472,   2.10640836,
      2.07979104,   2.05379508,   2.0283832,    2.00355544,   1.97931716,
      1.95564264,   1.93254512,   1.90997856,   1.88791516,   1.86637848,
      1.84527452,   1.82451708,   1.80413376,   1.78417816,   1.76469136,
      1.74556336,   1.72671164,   1.70821776,   1.69004908,   1.67225764,
      1.65488436,   1.63784684,   1.62109764,   1.60460048,   1.58845136,
      1.57270508,   1.55714604,   1.54188392,   1.52695208,   1.51240388,
      1.4981056,    1.48396928,   1.47016464,   1.45671476,   1.44358308,
      1.43073704,   1.41818224,   1.40593944,   1.39393144,   1.38221764,
      1.37082612,   1.35973732,   1.34886748,   1.33818336,   1.32769156,
      1.31741576,   1.30737756,   1.29757944,   1.28799968,   1.27861832,
      1.26948592,   1.26052252,   1.25172964,   1.24308336,   1.23461316,
      1.22635788,   1.21831004,   1.21044628,   1.20271912,   1.19515192,
      1.18778872,   1.18066096,   1.17369656,   1.16686096,   1.1601836,
      1.15366628,   0.369613524,  0.364863435,  0.360132602,  0.355533758,
      0.350967069,
]


def _read_group_bands(group):
    """Return ordered list of raster names from an imagery group."""
    ret = gs.read_command("i.group", flags="g", group=group, quiet=True)
    return [ln.strip() for ln in ret.strip().splitlines() if ln.strip()]


def _read_band_np(band_name, nr, nc, null_val=-9.999e+33):
    """Read a GRASS raster into float32 numpy array; NULL → NaN."""
    import numpy as np
    tmp = tempfile.mktemp(suffix=".bin")
    try:
        gs.run_command("r.out.bin", flags="f", input=band_name, output=tmp,
                       bytes=4, null=str(null_val), quiet=True)
        arr = np.fromfile(tmp, dtype=np.float32).reshape(nr, nc)
        arr[arr == null_val] = np.nan
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass
    return arr


def _write_band_np(arr, map_name, region, null_val=-9.999e+33):
    """Write float32 numpy array as GRASS FCELL raster."""
    import numpy as np
    tmp = tempfile.mktemp(suffix=".bin")
    try:
        out = np.where(np.isnan(arr), null_val, arr).astype(np.float32)
        out.tofile(tmp)
        gs.run_command(
            "r.in.bin", flags="f", input=tmp, output=map_name, bytes=4,
            north=region["n"], south=region["s"],
            east=region["e"],  west=region["w"],
            rows=int(region["rows"]), cols=int(region["cols"]),
            anull=str(null_val), overwrite=True, quiet=True,
        )
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass


def _load_solar_flux(solar_flux_path):
    """Return (wavelength_nm array, irradiance array) from 2-column ASCII file."""
    import numpy as np
    data = np.loadtxt(solar_flux_path)
    if data.ndim != 2 or data.shape[1] < 2:
        gs.fatal("solar_flux= file must have two columns: wavelength_nm, irradiance.")
    return data[:, 0], data[:, 1]


def main():
    import numpy as np

    opt_input    = options["input"]
    opt_output   = options["output"]
    opt_solar    = options["solar_flux"]
    emissivity   = float(options["emissivity"] or "0.95")
    wave_lo      = float(options["wave_lo"]    or "4500")
    wave_hi      = float(options["wave_hi"]    or "4874")
    flag_group   = flags["g"]

    # ── Read imagery group ────────────────────────────────────────────────
    bands = _read_group_bands(opt_input)
    if len(bands) != 256:
        gs.fatal(f"Expected 256 bands in group '{opt_input}', got {len(bands)}. "
                 "Import the full IIRS L2 cube and add all 256 bands to the group.")

    region = gs.region()
    nr = int(region["rows"])
    nc = int(region["cols"])

    # ── Load solar flux ───────────────────────────────────────────────────
    if opt_solar:
        wl_nm, solar_irr = _load_solar_flux(opt_solar)
        if len(wl_nm) != 256:
            gs.fatal(f"solar_flux= has {len(wl_nm)} rows; expected 256 (one per IIRS band).")
    else:
        wl_nm    = np.array(_SOLAR_FLUX_NM, dtype=np.float64)
        solar_irr = np.array(_SOLAR_FLUX_W,  dtype=np.float64)

    # ── Read full radiance cube (bands × rows × cols) ─────────────────────
    gs.message("Reading IIRS radiance cube …")
    cube = np.empty((256, nr, nc), dtype=np.float32)
    for b, band_name in enumerate(bands):
        cube[b] = _read_band_np(band_name, nr, nc)

    # Strip 7 bad bands at the start and 2 at the end; scale DN → radiance
    rad = cube[7:-2].astype(np.float64) * 0.01   # (247, nr, nc)
    del cube

    L_nm  = wl_nm[7:-2]      # (247,) wavelengths in nm
    L_irr = solar_irr[7:-2]  # (247,) solar irradiance (already in correct units)
    # Scale irradiance as in the QGIS plugin (×10 to match the stored radiance scale)
    L_irr = np.round(L_irr, 4) * 10.0

    lmb = L_nm * 1e-9          # (247,) wavelengths in metres

    # ── Planck inversion: per-pixel temperature from thermal window ────────
    mask_th = (L_nm >= wave_lo) & (L_nm <= wave_hi)
    idx_lo = int(np.argmax(mask_th))
    idx_hi = int(len(mask_th) - np.argmax(mask_th[::-1]))   # exclusive

    gs.message(f"Fitting temperature from bands {idx_lo}-{idx_hi-1} "
               f"({L_nm[idx_lo]:.0f}-{L_nm[idx_hi-1]:.0f} nm) …")

    ra_th  = rad[idx_lo:idx_hi]                              # (n_th, nr, nc)
    lmb_th = lmb[idx_lo:idx_hi, np.newaxis, np.newaxis]     # (n_th, 1, 1)

    # T per-band: invert Planck: L = ε·B → B = L/ε
    # B = (2hc²/λ⁵) / (exp(hc/(λkT)) - 1) × 1e-6 (µm normalisation)
    # Rearranging: T = hc / (λk · ln(ε·2hc²·1e-6 / (B·λ⁵) + 1))
    with np.errstate(divide="ignore", invalid="ignore"):
        q = np.log(emissivity * 2 * _H * _C * _C * 1e-6
                   / (ra_th * lmb_th**5) + 1.0)
        T_per_band = _H * _C / (lmb_th * _K * q)   # (n_th, nr, nc)

    mT = np.nanmean(T_per_band, axis=0)             # (nr, nc) surface temperature
    del T_per_band, ra_th

    # ── Thermal emission for all wavelengths ──────────────────────────────
    lmb_col = lmb[:, np.newaxis, np.newaxis]         # (247, 1, 1)
    mT_row  = mT[np.newaxis, :, :]                   # (1, nr, nc)
    with np.errstate(over="ignore", invalid="ignore"):
        RT = (1e-6) * (2 * _H * _C * _C / lmb_col**5) \
             / (np.exp(_H * _C / (lmb_col * _K * mT_row)) - 1.0)  # (247, nr, nc)

    # ── Reflectance: R = π(L - ε·RT) / F_solar ───────────────────────────
    Llambda = L_irr[:, np.newaxis, np.newaxis]       # (247, 1, 1)
    outRef = np.pi * (rad - emissivity * RT) / Llambda
    outRef[outRef < 0] = 0.0
    del rad, RT

    # ── 3-point moving average over band axis ─────────────────────────────
    n = outRef.shape[0]
    a1 = np.concatenate([outRef, np.zeros((2, nr, nc))], axis=0)
    a2 = np.concatenate([np.zeros((1, nr, nc)), outRef, np.zeros((1, nr, nc))], axis=0)
    a3 = np.concatenate([np.zeros((2, nr, nc)), outRef], axis=0)
    interior = (a1 + a2 + a3)[2:-2] / 3.0           # (245, nr, nc)
    first = (outRef[1:2] + outRef[2:3]) / 2.0
    last_ = (outRef[-1:] + outRef[-2:-1]) / 2.0
    outRef = np.concatenate([first, interior, last_], axis=0)  # (247, nr, nc)

    # ── Output wavelength window: 800-5000 nm ────────────────────────────
    out_mask = (L_nm >= 800) & (L_nm <= 5000)
    idx_out_lo = int(np.argmax(out_mask))
    idx_out_hi = int(len(out_mask) - np.argmax(out_mask[::-1]))
    outRef_sub = outRef[idx_out_lo:idx_out_hi]        # (n_out, nr, nc)
    L_nm_out   = L_nm[idx_out_lo:idx_out_hi]
    n_out = outRef_sub.shape[0]

    # ── Per-band correction coefficients ─────────────────────────────────
    coeff = np.array(_IIRS_COEFF, dtype=np.float64)
    if len(coeff) != n_out:
        gs.warning(f"Coefficient array length ({len(coeff)}) != output bands ({n_out}); "
                   "skipping coefficient correction.")
    else:
        outRef_sub /= coeff[:, np.newaxis, np.newaxis]

    # ── Write reflectance bands ───────────────────────────────────────────
    gs.message(f"Writing {n_out} reflectance bands ({L_nm_out[0]:.0f}-{L_nm_out[-1]:.0f} nm) …")
    out_maps = []
    for i in range(n_out):
        map_name = f"{opt_output}.b{i+1:03d}"
        _write_band_np(outRef_sub[i].astype(np.float32), map_name, region)
        gs.run_command("r.support", map=map_name,
                       title=f"IIRS corrected reflectance band {i+1}",
                       description=f"wavelength {L_nm_out[i]:.1f} nm",
                       quiet=True)
        out_maps.append(map_name)

    # ── Write temperature ─────────────────────────────────────────────────
    temp_name = f"{opt_output}.temp"
    _write_band_np(mT.astype(np.float32), temp_name, region)
    gs.run_command("r.support", map=temp_name,
                   title="IIRS surface temperature", units="K", quiet=True)
    gs.run_command("r.colors", map=temp_name, color="bcyr", quiet=True)

    # ── Create output imagery group ───────────────────────────────────────
    if flag_group:
        gs.run_command("i.group", group=opt_output,
                       input=",".join(out_maps), quiet=True)
        gs.message(f"Created imagery group '{opt_output}' with {n_out} bands.")

    gs.message(f"Done. Thermally-corrected reflectance: {opt_output}.b001 .. "
               f"{opt_output}.b{n_out:03d}; temperature: {temp_name}.")
    gs.message(f"  Temperature range (sampled): {np.nanmin(mT):.1f} K - {np.nanmax(mT):.1f} K")


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
