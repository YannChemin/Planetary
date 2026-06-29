/****************************************************************************
 *
 * MODULE:       p.mineral.indices
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute body-specific planetary mineral spectral indices.
 *               Mars: olivine BD(1.05µm), pyroxene BD(2.0µm), TiO2/FeO ratios.
 *               Moon/M3: IBD1000, IBD2000, R(1580/1250), BD(2800).
 *               Mercury/MDIS: R(749/433) maturity, R(996/749) mafic, slope.
 *               Titan/VIMS: R(5.0/2.0), R(2.8/2.0), R(1.59/1.27).
 *               Venus/VIRTIS: R(1.74/1.30), R(1.18/1.10), BD(2.3µm).
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include "../../libs/p_spectra/p_spectra.h"

/* ── Index algorithm codes ────────────────────────────────────────────────── */
#define IDX_BD    0   /* 3-point band depth */
#define IDX_RATIO 1   /* band ratio wl_center/wl_right */
#define IDX_IBD   2   /* integrated band depth (sum of BD over range) */
#define IDX_SLOPE 3   /* linear spectral slope (units: per µm) */

#define MAX_IBD 20

typedef struct {
    const char *name;
    const char *description;
    const char *body;          /* "any" | "moon" | "mercury" | "titan" | "venus" */
    int   mode;
    /* BD / RATIO / SLOPE */
    double wl_center;
    double wl_left;
    double wl_right;
    /* IBD (overrides above when mode==IDX_IBD) */
    int    n_ibd;
    double ibd_centers[MAX_IBD];
    double ibd_left;
    double ibd_right;
} MineralIndex;

/* ── M3 IBD wavelength sets (Clark et al. 2011) ──────────────────────────── */
/* IBD1000: continuum 0.749→1.309; 10 centres at 0.030 µm spacing            */
static const double IBD1000_C[] = {
    0.819, 0.849, 0.879, 0.919, 0.959, 1.009, 1.049, 1.089, 1.139, 1.189
};
/* IBD2000: continuum 1.579→2.499; 10 centres at ~0.09 µm spacing            */
static const double IBD2000_C[] = {
    1.659, 1.759, 1.859, 1.959, 2.059, 2.159, 2.259, 2.359, 2.459, 2.499
};
#define IBD_N 10

/* ── Index table ──────────────────────────────────────────────────────────── */
/* body="any" indices are shown for all bodies; body-specific ones only when  */
/* body= matches.                                                              */
static MineralIndex INDICES[] = {

    /* ── Mars / generic ───────────────────────────────────────────────────── */
    { "olivine",  "Olivine band depth at 1.05 µm",               "any",
      IDX_BD,    1.05, 0.75, 1.55, 0, {0}, 0, 0 },
    { "pyroxene", "Pyroxene band depth at 2.0 µm",               "any",
      IDX_BD,    2.0,  1.5,  2.5,  0, {0}, 0, 0 },
    { "tio2",     "TiO2: R(415nm)/R(750nm) Clementine ratio",    "any",
      IDX_RATIO, 0.415, 0.0, 0.750, 0, {0}, 0, 0 },
    { "feo",      "FeO: R(950nm)/R(750nm) Clementine ratio",     "any",
      IDX_RATIO, 0.950, 0.0, 0.750, 0, {0}, 0, 0 },
    { "mafic",    "Mafic BD: olivine (1.05µm) + pyroxene (2.0µm)","any",
      IDX_IBD,   0, 0, 0,
      2, { 1.05, 2.0 }, 0.75, 2.5 },

    /* ── Moon / M3 (Clark et al. 2011, J. Geophys. Res.) ─────────────────── */
    { "ibd1000",
      "M3 IBD1000: Integrated Band Depth 0.82–1.19 µm (olivine+pyroxene)",
      "moon",
      IDX_IBD,   0, 0, 0,
      IBD_N, { 0 }, 0.749, 1.309 },   /* centres filled in init() */

    { "ibd2000",
      "M3 IBD2000: Integrated Band Depth 1.66–2.50 µm (pyroxene dominant)",
      "moon",
      IDX_IBD,   0, 0, 0,
      IBD_N, { 0 }, 1.579, 2.499 },

    { "r1580_1250",
      "M3 Hydroxyl index: R(1.58µm)/R(1.25µm) — OH/H2O overtone",
      "moon",
      IDX_RATIO, 1.58, 0.0, 1.25,  0, {0}, 0, 0 },

    { "bd2800",
      "M3 OH/H2O: Band depth at 2.8 µm (Clark 2011 Table 2)",
      "moon",
      IDX_BD,    2.8,  2.5,  3.1,   0, {0}, 0, 0 },

    /* ── Mercury / MDIS (Denevi et al. 2009, 2016) ────────────────────────── */
    { "r749_433",
      "MDIS maturity index: R(749nm)/R(433nm) — high = red/space-weathered",
      "mercury",
      IDX_RATIO, 0.749, 0.0, 0.433, 0, {0}, 0, 0 },

    { "r996_749",
      "MDIS mafic index: R(996nm)/R(749nm) — pyroxene/mafic absorption",
      "mercury",
      IDX_RATIO, 0.996, 0.0, 0.749, 0, {0}, 0, 0 },

    { "spec_slope",
      "MDIS spectral slope 433–996 nm (reflectance per µm; negative = blue unit)",
      "mercury",
      IDX_SLOPE, 0.433, 0.0, 0.996, 0, {0}, 0, 0 },

    /* ── Titan / VIMS (Soderblom et al. 2007, Icarus 194) ────────────────── */
    { "r500_200",
      "VIMS dark-material index: R(5.0µm)/R(2.0µm) — high = dark tholin-rich",
      "titan",
      IDX_RATIO, 5.0,  0.0, 2.0,   0, {0}, 0, 0 },

    { "r280_200",
      "VIMS water-ice index: R(2.8µm)/R(2.0µm) — H2O ice has deep 2.0µm absorption",
      "titan",
      IDX_RATIO, 2.8,  0.0, 2.0,   0, {0}, 0, 0 },

    { "r159_127",
      "VIMS hydrocarbon index: R(1.59µm)/R(1.27µm) — distinguishes HC ice species",
      "titan",
      IDX_RATIO, 1.59, 0.0, 1.27,  0, {0}, 0, 0 },

    /* ── Venus / VIRTIS (Meadows&Crisp 1996; Bézard et al. 2009) ─────────── */
    { "r1740_1300",
      "VIRTIS surface emission: R(1.74µm)/R(1.30µm) — 1.74µm window/continuum",
      "venus",
      IDX_RATIO, 1.74, 0.0, 1.30,  0, {0}, 0, 0 },

    { "r1180_1100",
      "VIRTIS surface emission: R(1.18µm)/R(1.10µm) — 1.18µm window",
      "venus",
      IDX_RATIO, 1.18, 0.0, 1.10,  0, {0}, 0, 0 },

    { "bd2300",
      "VIRTIS CO2/surface: Band depth at 2.3 µm emission window",
      "venus",
      IDX_BD,    2.3,  2.0, 2.55,  0, {0}, 0, 0 },

    { NULL, NULL, NULL, 0, 0, 0, 0, 0, {0}, 0, 0 }
};

/* Fill the zeroed IBD centre arrays from the static const arrays. */
static void init_ibd_centres(void)
{
    for (int i = 0; INDICES[i].name; i++) {
        if (INDICES[i].mode != IDX_IBD) continue;
        if (strcmp(INDICES[i].name, "ibd1000") == 0)
            memcpy(INDICES[i].ibd_centers, IBD1000_C, IBD_N * sizeof(double));
        else if (strcmp(INDICES[i].name, "ibd2000") == 0)
            memcpy(INDICES[i].ibd_centers, IBD2000_C, IBD_N * sizeof(double));
        /* mafic: centres already set inline */
    }
}

/* Build a comma-separated options string for all indices matching body. */
static char *build_options(const char *body)
{
    static char buf[1024];
    buf[0] = '\0';
    for (int i = 0; INDICES[i].name; i++) {
        const char *b = INDICES[i].body;
        if (strcmp(b, "any") != 0 && strcmp(b, body) != 0) continue;
        if (buf[0]) strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, INDICES[i].name, sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_index, *opt_wcsv, *opt_body;
    struct History  history;

    init_ibd_centres();

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("spectral indices"));
    G_add_keyword(_("mineralogy"));
    G_add_keyword(_("CRISM"));
    G_add_keyword(_("M3"));
    G_add_keyword(_("MDIS"));
    G_add_keyword(_("VIMS"));
    G_add_keyword(_("VIRTIS"));
    module->label = _("Compute body-specific planetary mineral spectral indices.");
    module->description = _(
        "Calculates band-depth, band-ratio, integrated band-depth, or spectral-slope "
        "indices for mineral identification from multi-band imagery. "
        "body=mars: olivine, pyroxene, TiO2, FeO, mafic. "
        "body=moon: M3 IBD1000/IBD2000, R(1580/1250), BD(2800). "
        "body=mercury: MDIS maturity/mafic/slope. "
        "body=titan: VIMS 5.0/2.8/2.0/1.59/1.27 µm ratios. "
        "body=venus: VIRTIS 1.74/1.18 µm emission windows, BD(2.3µm). "
        "Input bands: <input>.1, <input>.2, …");

    opt_body = G_define_option();
    opt_body->key = "body";
    opt_body->type = TYPE_STRING;
    opt_body->required = NO;
    opt_body->options = "mars,moon,mercury,titan,venus";
    opt_body->answer  = "mars";
    opt_body->description = _("Target planetary body (selects the available index set)");
    opt_body->descriptions = _(
        "mars;Mars/generic — olivine, pyroxene, TiO2, FeO, mafic;"
        "moon;Moon/M3 — IBD1000, IBD2000, hydroxyl, BD(2800) + generic;"
        "mercury;Mercury/MDIS — maturity, mafic, spectral slope + generic;"
        "titan;Saturn's Titan/VIMS — 5.0/2.8/2.0/1.59/1.27 µm ratios + generic;"
        "venus;Venus/VIRTIS — 1.74/1.18 µm emission windows, BD(2.3µm) + generic");

    opt_input = G_define_option();
    opt_input->key  = "input";
    opt_input->type = TYPE_STRING;
    opt_input->required = YES;
    opt_input->description = _("Base name of input band rasters (input.1, input.2, …)");

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_index = G_define_option();
    opt_index->key         = "index";
    opt_index->type        = TYPE_STRING;
    opt_index->required    = YES;
    /* Options string is set dynamically after G_parser reads body= — but GRASS
       requires options at definition time; we list all and validate in code. */
    opt_index->options     = "olivine,pyroxene,tio2,feo,mafic,"
                             "ibd1000,ibd2000,r1580_1250,bd2800,"
                             "r749_433,r996_749,spec_slope,"
                             "r500_200,r280_200,r159_127,"
                             "r1740_1300,r1180_1100,bd2300";
    opt_index->description = _("Spectral index to compute (available choices depend on body=)");

    opt_wcsv = G_define_option();
    opt_wcsv->key  = "wavelengths";
    opt_wcsv->type = TYPE_STRING;
    opt_wcsv->required = NO;
    opt_wcsv->description = _("CSV file with wavelength (µm) per band, one per line");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *body     = opt_body->answer;
    const char *inbase   = opt_input->answer;
    const char *idx_name = opt_index->answer;

    /* Find index spec, checking body compatibility */
    const MineralIndex *midx = NULL;
    for (int i = 0; INDICES[i].name; i++) {
        if (strcmp(INDICES[i].name, idx_name) != 0) continue;
        const char *b = INDICES[i].body;
        if (strcmp(b, "any") == 0 || strcmp(b, body) == 0) {
            midx = &INDICES[i]; break;
        }
    }
    if (!midx) {
        G_message(_("Available indices for body=%s: %s"), body, build_options(body));
        G_fatal_error(_("Index '%s' is not available for body='%s'"), idx_name, body);
    }

    G_message(_("body=%s  index=%s — %s"), body, midx->name, midx->description);

    /* Count bands */
    int nbands = 0;
    char mapname[512];
    for (int b = 1; b <= 10000; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b);
        if (!G_find_raster(mapname, "")) break;
        nbands++;
    }
    if (nbands < 2)
        G_fatal_error(_("Need at least 2 bands; found %d for '%s'"), nbands, inbase);

    /* Spectral definition */
    PSpectraDef *sd;
    if (opt_wcsv->answer) {
        sd = p_spectra_def_read_csv(opt_wcsv->answer);
    } else {
        /* Fallback: linear wavelength grid 0.4 µm → 0.4 + nbands*Δ        */
        /* The actual Δ doesn't affect ratio/slope indices; BD/IBD need the  */
        /* correct wavelengths — always supply wavelengths= for real data.   */
        double *wl = (double *)G_malloc((size_t)nbands * sizeof(double));
        double *wd = (double *)G_malloc((size_t)nbands * sizeof(double));
        for (int b = 0; b < nbands; b++) { wl[b] = 0.4 + b * 0.01; wd[b] = 0.01; }
        sd = p_spectra_def_create(nbands, wl, wd);
        G_free(wl); G_free(wd);
        G_warning(_("No wavelengths= CSV supplied — using a 10 nm fallback grid "
                    "starting at 0.4 µm. Results may be incorrect for BD/IBD indices."));
    }
    if (!sd) G_fatal_error(_("Cannot create spectral definition"));

    /* Open input rasters */
    int *fd_in = (int *)G_malloc((size_t)nbands * sizeof(int));
    for (int b = 0; b < nbands; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b + 1);
        fd_in[b] = Rast_open_old(mapname, "");
    }
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);

    struct Cell_head reg;
    G_get_window(&reg);
    int nrows = reg.rows, ncols = reg.cols;

    DCELL **bufs    = (DCELL **)G_malloc((size_t)nbands * sizeof(DCELL *));
    DCELL  *buf_out = Rast_allocate_d_buf();
    for (int b = 0; b < nbands; b++) bufs[b] = Rast_allocate_d_buf();
    double *spec  = (double *)G_malloc((size_t)ncols * nbands * sizeof(double));
    double *out_d = (double *)G_malloc((size_t)ncols * sizeof(double));

    /* Pre-build IBD weight / left / right arrays (all-equal weights, same L/R) */
    double ibd_wl[MAX_IBD], ibd_ll[MAX_IBD], ibd_rr[MAX_IBD], ibd_wt[MAX_IBD];
    if (midx->mode == IDX_IBD) {
        for (int k = 0; k < midx->n_ibd; k++) {
            ibd_wl[k] = midx->ibd_centers[k];
            ibd_ll[k] = midx->ibd_left;
            ibd_rr[k] = midx->ibd_right;
            ibd_wt[k] = 1.0;
        }
    }

    for (int row = 0; row < nrows; row++) {
        G_percent(row, nrows, 2);
        for (int b = 0; b < nbands; b++) {
            Rast_get_d_row(fd_in[b], bufs[b], row);
            for (int c = 0; c < ncols; c++)
                spec[(size_t)c * nbands + b] =
                    Rast_is_d_null_value(&bufs[b][c]) ? NAN : (double)bufs[b][c];
        }

        switch (midx->mode) {

        case IDX_BD:
            p_spectra_apply_row_band_depth(sd, ncols, nbands, spec,
                midx->wl_center, midx->wl_left, midx->wl_right, 0, out_d);
            break;

        case IDX_RATIO:
            p_spectra_apply_row_band_ratio(sd, ncols, nbands, spec,
                midx->wl_center, midx->wl_right, 0, out_d);
            break;

        case IDX_IBD:
            p_spectra_apply_row_bd_multi(sd, ncols, nbands, spec,
                midx->n_ibd, ibd_wl, ibd_ll, ibd_rr, ibd_wt, 0, out_d);
            break;

        case IDX_SLOPE: {
            /* Normalised spectral slope: (R_hi/R_lo - 1) / (wl_hi - wl_lo)
               wl_center = lo wavelength, wl_right = hi wavelength.             */
            double dw = midx->wl_right - midx->wl_center;
            /* band_ratio(wl_right, wl_center) = R(hi)/R(lo)                    */
            p_spectra_apply_row_band_ratio(sd, ncols, nbands, spec,
                midx->wl_right, midx->wl_center, 0, out_d);
            for (int c = 0; c < ncols; c++) {
                if (out_d[c] != out_d[c]) continue;
                out_d[c] = (out_d[c] - 1.0) / dw;
            }
            break;
        }

        default:
            G_fatal_error(_("Unknown index mode %d"), midx->mode);
        }

        for (int c = 0; c < ncols; c++) {
            if (out_d[c] != out_d[c])
                Rast_set_d_null_value(&buf_out[c], 1);
            else
                buf_out[c] = (DCELL)out_d[c];
        }
        Rast_put_d_row(fd_out, buf_out);
    }
    G_percent(1, 1, 2);

    for (int b = 0; b < nbands; b++) {
        Rast_close(fd_in[b]);
        G_free(bufs[b]);
    }
    G_free(fd_in); G_free(bufs); G_free(buf_out); G_free(spec); G_free(out_d);
    Rast_close(fd_out);
    p_spectra_def_free(sd);

    Rast_short_history(opt_output->answer, "raster", &history);
    Rast_command_history(&history);
    Rast_write_history(opt_output->answer, &history);
    G_message(_("Index '%s' written: %s"), midx->name, opt_output->answer);
    return EXIT_SUCCESS;
}
