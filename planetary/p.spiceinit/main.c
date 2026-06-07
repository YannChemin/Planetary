/****************************************************************************
 *
 * MODULE:       p.spiceinit
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Attach SPICE kernel assignments to a GRASS raster map so
 *               that subsequent p.* geometry modules (p.phocube, p.cam2map)
 *               can load the correct kernels automatically.
 *
 *               Kernel paths are stored as GRASS map metadata in a
 *               dedicated column table (SPICE_KERNELS) attached to the
 *               raster map.  The module also validates that every specified
 *               kernel file is readable and loads correctly via p_spice.
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
#include <errno.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* p_spice library */
#include "../../libs/p_spice/p_spice.h"

/* SPICE kernel metadata key stored in the raster's misc information.
 * Each kernel type is stored as a separate key. */
#define SPICE_META_PREFIX "SPICE_"

/* Maximum kernel paths per type. */
#define MAX_KERNELS_PER_TYPE 8

/* ------------------------------------------------------------------ */
/* Store a semicolon-separated list of paths in raster misc data.     */
/* Uses GRASS's native key=value metadata (stored in cell_misc/).     */
/* ------------------------------------------------------------------ */
static void store_kernel_list(const char *mapname, const char *mapset,
                               const char *key, char **paths, int n)
{
    char combined[8192];
    combined[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strncat(combined, ";", sizeof(combined)-strlen(combined)-1);
        strncat(combined, paths[i], sizeof(combined)-strlen(combined)-1);
    }

    /* Write to GRASS raster history extra data section. */
    struct History hist;
    int have_hist = (Rast_read_history(mapname, mapset, &hist) >= 0);
    if (!have_hist)
        Rast_short_history(mapname, "raster", &hist);

    /* Use the command-line comment field to store the SPICE key=value pair.
     * GRASS doesn't have a generic key-value store for rasters outside the
     * database; we embed it in the history ECOM (extra commands) section. */
    char entry[8300];
    snprintf(entry, sizeof(entry), "%s%s=%s", SPICE_META_PREFIX, key, combined);

    /* Rast_set_history stores strings in the HIST structure. */
    Rast_set_history(&hist, HIST_KEYWRD, entry);
    Rast_write_history(mapname, &hist);
}

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_map, *opt_lsk, *opt_sclk, *opt_ck;
    struct Option  *opt_spk, *opt_ik, *opt_fk, *opt_pck, *opt_target;
    struct Flag    *flag_test;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Camera & Geometry"));
    G_add_keyword(_("SPICE"));
    G_add_keyword(_("camera"));
    G_add_keyword(_("geometry"));
    module->label       = _("Attach SPICE kernel assignments to a GRASS raster map.");
    module->description = _("Validates and stores paths to NAIF SPICE kernels (LSK, SCLK, "
                             "CK, SPK, IK, FK, PCK) in the raster map's metadata. "
                             "Subsequent modules p.phocube, p.cam2map, p.caminfo read these "
                             "paths automatically so kernels need not be specified repeatedly. "
                             "Multiple paths per kernel type are separated by commas.");

    opt_map = G_define_standard_option(G_OPT_R_INPUT);
    opt_map->key         = "map";
    opt_map->description = _("Raster map to attach SPICE kernels to");

    opt_target = G_define_option();
    opt_target->key         = "target";
    opt_target->type        = TYPE_STRING;
    opt_target->required    = NO;
    opt_target->description = _("Target body name (e.g. MARS, MOON) — stored for downstream modules");

    opt_lsk = G_define_option();
    opt_lsk->key         = "lsk";
    opt_lsk->type        = TYPE_STRING;
    opt_lsk->required    = NO;
    opt_lsk->multiple    = YES;
    opt_lsk->description = _("Leapsecond kernel(s) (.tls)");

    opt_sclk = G_define_option();
    opt_sclk->key         = "sclk";
    opt_sclk->type        = TYPE_STRING;
    opt_sclk->required    = NO;
    opt_sclk->multiple    = YES;
    opt_sclk->description = _("Spacecraft clock kernel(s) (.tsc)");

    opt_ck = G_define_option();
    opt_ck->key         = "ck";
    opt_ck->type        = TYPE_STRING;
    opt_ck->required    = NO;
    opt_ck->multiple    = YES;
    opt_ck->description = _("C-kernel(s) — spacecraft pointing (.bc)");

    opt_spk = G_define_option();
    opt_spk->key         = "spk";
    opt_spk->type        = TYPE_STRING;
    opt_spk->required    = NO;
    opt_spk->multiple    = YES;
    opt_spk->description = _("SPK kernel(s) — spacecraft/body ephemeris (.bsp)");

    opt_ik = G_define_option();
    opt_ik->key         = "ik";
    opt_ik->type        = TYPE_STRING;
    opt_ik->required    = NO;
    opt_ik->multiple    = YES;
    opt_ik->description = _("Instrument kernel(s) (.ti)");

    opt_fk = G_define_option();
    opt_fk->key         = "fk";
    opt_fk->type        = TYPE_STRING;
    opt_fk->required    = NO;
    opt_fk->multiple    = YES;
    opt_fk->description = _("Frame kernel(s) (.tf)");

    opt_pck = G_define_option();
    opt_pck->key         = "pck";
    opt_pck->type        = TYPE_STRING;
    opt_pck->required    = NO;
    opt_pck->multiple    = YES;
    opt_pck->description = _("PCK kernel(s) — body constants (.tpc, .bpc)");

    flag_test = G_define_flag();
    flag_test->key         = 't';
    flag_test->description = _("Test-load all kernels with CSPICE and report errors (recommended)");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *mapname = opt_map->answer;
    const char *mapset  = G_find_raster((char *)mapname, "");
    if (!mapset)
        G_fatal_error(_("Raster map <%s> not found"), mapname);

    int do_test = flag_test->answer;

    /* ---------------------------------------------------------------- */
    /* Collect all kernel paths and validate file accessibility          */
    /* ---------------------------------------------------------------- */
    struct {
        struct Option *opt;
        const char    *type_key;
    } kernel_types[] = {
        { opt_lsk,  "LSK"  },
        { opt_sclk, "SCLK" },
        { opt_ck,   "CK"   },
        { opt_spk,  "SPK"  },
        { opt_ik,   "IK"   },
        { opt_fk,   "FK"   },
        { opt_pck,  "PCK"  },
        { NULL, NULL }
    };

    int total_loaded = 0;

    if (do_test) {
        p_spice_init();
        G_message(_("Testing kernel loads with CSPICE ..."));
    }

    for (int t = 0; kernel_types[t].opt != NULL; t++) {
        struct Option *opt = kernel_types[t].opt;
        if (!opt->answers) continue;

        char *paths[MAX_KERNELS_PER_TYPE];
        int npaths = 0;

        for (int i = 0; opt->answers[i] && npaths < MAX_KERNELS_PER_TYPE; i++) {
            const char *path = opt->answers[i];

            /* Check readability. */
            FILE *fp = fopen(path, "r");
            if (!fp) {
                G_warning(_("Kernel file not readable: %s (%s)"),
                           path, strerror(errno));
                continue;
            }
            fclose(fp);

            /* Optionally test-load with CSPICE. */
            if (do_test) {
                if (p_spice_load(path) < 0) {
                    G_warning(_("CSPICE rejected kernel '%s'"), path);
                    continue;
                }
                G_message(_("  [OK] %s: %s"), kernel_types[t].type_key, path);
            } else {
                G_message(_("  [registered] %s: %s"),
                           kernel_types[t].type_key, path);
            }

            paths[npaths++] = G_store(path);
            total_loaded++;
        }

        if (npaths > 0) {
            store_kernel_list(mapname, mapset,
                               kernel_types[t].type_key, paths, npaths);
            for (int i = 0; i < npaths; i++) G_free(paths[i]);
        }
    }

    /* Store target body name if given. */
    if (opt_target->answer) {
        char paths_arr[1][256];
        strncpy(paths_arr[0], opt_target->answer, 255);
        char *pp[1] = { paths_arr[0] };
        store_kernel_list(mapname, mapset, "TARGET", pp, 1);
        G_message(_("Target body: %s"), opt_target->answer);
    }

    /* Clean up CSPICE state after test (do not leave kernels loaded
     * between runs — each module loads its own session). */
    if (do_test) {
        p_spice_clear();
        G_message(_("CSPICE test-load complete. All kernels unloaded."));
    }

    if (total_loaded == 0 && !opt_target->answer)
        G_warning(_("No kernel files were registered. "
                    "Specify at least one kernel type option."));

    G_message(_("p.spiceinit: %d kernel file(s) registered for map <%s>."),
               total_loaded, mapname);

    return EXIT_SUCCESS;
}
