/****************************************************************************
 *
 * MODULE:       p.in.isis
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Import an ISIS3 cube (.cub) into GRASS raster map(s)
 *               via GDAL's built-in ISIS3 driver.
 *
 *               Delegates to r.in.gdal for the actual import, then
 *               stores ISIS3 label metadata in the map's history and
 *               (optionally) a vector attribute table.
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

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* ------------------------------------------------------------------ */
/* Minimal PVL reader (subset of p_pds) to extract ISIS3 label fields */
/* ------------------------------------------------------------------ */

static char *pvl_get(const char *label_path, const char *keyword)
{
    FILE *fp = fopen(label_path, "r");
    if (!fp) return NULL;

    static char result[1024];
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* Check keyword match (case-insensitive) */
        int klen = (int)strlen(keyword);
        if (G_strncasecmp(p, keyword, klen) == 0) {
            char *eq = strchr(p + klen, '=');
            if (eq) {
                char *val = eq + 1;
                while (*val == ' ' || *val == '\t') val++;
                /* Strip trailing newline/whitespace */
                int vlen = (int)strlen(val);
                while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r'
                                    || val[vlen-1] == ' '))
                    val[--vlen] = '\0';
                strncpy(result, val, sizeof(result)-1);
                fclose(fp);
                return result;
            }
        }
        /* Stop at END */
        if (strncmp(p, "End\n", 4) == 0 || strcmp(p, "End") == 0) break;
    }
    fclose(fp);
    return NULL;
}

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Flag    *flag_proj, *flag_region, *flag_group;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Import & Export"));
    G_add_keyword(_("import"));
    G_add_keyword(_("raster"));
    G_add_keyword(_("ISIS3"));
    G_add_keyword(_("ISIS"));
    module->label       = _("Import an ISIS3 cube into GRASS raster map(s).");
    module->description = _("Reads an ISIS3 .cub file using GDAL's built-in ISIS3 driver. "
                             "Equivalent to 'r.in.gdal' with additional ISIS3 metadata "
                             "extraction (target body, band wavelengths, spiceinit status) "
                             "stored in the raster history.");

    opt_input = G_define_option();
    opt_input->key         = "input";
    opt_input->type        = TYPE_STRING;
    opt_input->required    = YES;
    opt_input->description = _("Path to the ISIS3 cube file (.cub)");
    opt_input->gisprompt   = "old_file,file,input";

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Name for output raster map");

    flag_proj = G_define_flag();
    flag_proj->key         = 'j';
    flag_proj->description = _("Set the GRASS projection from the ISIS3 cube's map projection");

    flag_region = G_define_flag();
    flag_region->key       = 'e';
    flag_region->description = _("Extend region to fit the imported map");

    flag_group = G_define_flag();
    flag_group->key         = 'g';
    flag_group->description = _("Register output maps in a GRASS imagery group");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *input   = opt_input->answer;
    const char *outbase = opt_output->answer;

    /* ---------------------------------------------------------------- */
    /* Delegate pixel data import to r.in.gdal                          */
    /* ---------------------------------------------------------------- */
    char gdal_cmd[4096];
    snprintf(gdal_cmd, sizeof(gdal_cmd),
             "r.in.gdal input=%s output=%s%s%s",
             input, outbase,
             flag_proj->answer   ? " -j" : "",
             flag_region->answer ? " -e" : "");

    G_message(_("Importing ISIS3 cube via r.in.gdal ..."));
    int rc = system(gdal_cmd);
    if (rc != 0)
        G_fatal_error(_("r.in.gdal failed for '%s'"), input);

    /* ---------------------------------------------------------------- */
    /* Extract ISIS3 label metadata                                      */
    /* ---------------------------------------------------------------- */
    char *target = pvl_get(input, "TargetName");
    char *start  = pvl_get(input, "StartTime");
    char *stop   = pvl_get(input, "StopTime");
    char *inst   = pvl_get(input, "InstrumentId");
    char *sc     = pvl_get(input, "SpacecraftName");

    G_message(_("ISIS3 metadata:"));
    if (sc)     G_message(_("  Spacecraft  : %s"), sc);
    if (inst)   G_message(_("  Instrument  : %s"), inst);
    if (target) G_message(_("  Target body : %s"), target);
    if (start)  G_message(_("  Start time  : %s"), start);
    if (stop)   G_message(_("  Stop time   : %s"), stop);

    /* Write metadata into the raster history (for whichever bands were created). */
    /* Attempt outbase, outbase.1, outbase.2, … (stop at first not found). */
    for (int b = 0; b <= 9999; b++) {
        char mapname[512];
        if (b == 0) snprintf(mapname, sizeof(mapname), "%s", outbase);
        else        snprintf(mapname, sizeof(mapname), "%s.%d", outbase, b);

        /* Check if this map exists in the current mapset. */
        if (!G_find_raster((char *)mapname, G_mapset())) {
            if (b == 0) break;    /* single-band: only outbase exists, and it wasn't found */
            break;
        }

        Rast_short_history(mapname, "raster", &history);
        if (sc)     Rast_set_history(&history, HIST_DATSRC_1, sc);
        if (inst)   Rast_set_history(&history, HIST_DATSRC_2, inst);
        if (target) Rast_set_history(&history, HIST_MAPID,    target);
        Rast_command_history(&history);
        Rast_write_history(mapname, &history);
    }

    /* ---------------------------------------------------------------- */
    /* Optional imagery group                                            */
    /* ---------------------------------------------------------------- */
    if (flag_group->answer) {
        /* Count how many maps were created. */
        int count = 0;
        char mapname[512];
        snprintf(mapname, sizeof(mapname), "%s", outbase);
        if (G_find_raster((char *)mapname, G_mapset())) count = 1;
        for (int b = 1; b <= 9999; b++) {
            snprintf(mapname, sizeof(mapname), "%s.%d", outbase, b);
            if (!G_find_raster((char *)mapname, G_mapset())) break;
            count++;
        }

        if (count > 1) {
            char group_cmd[8192];
            snprintf(group_cmd, sizeof(group_cmd),
                     "i.group group=%s subgroup=%s input=", outbase, outbase);
            for (int b = 1; b <= count; b++) {
                char mn[512];
                snprintf(mn, sizeof(mn), "%s.%d", outbase, b);
                strncat(group_cmd, mn, sizeof(group_cmd)-strlen(group_cmd)-2);
                if (b < count) strncat(group_cmd, ",", 2);
            }
            system(group_cmd);
            G_message(_("Imagery group '%s' created (%d maps)."), outbase, count);
        }
    }

    G_message(_("ISIS3 import complete: %s"), outbase);
    return EXIT_SUCCESS;
}
