/****************************************************************************
 *
 * MODULE:       p.out.isis
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Export a GRASS raster map to an ISIS3 cube (.cub) file.
 *
 *               Delegates pixel data export to r.out.gdal with the ISIS3
 *               GDAL driver, then appends PVL metadata keywords (target
 *               body, instrument, band wavelengths, GRASS provenance).
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_target, *opt_inst;
    struct Option  *opt_type;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Import & Export"));
    G_add_keyword(_("export"));
    G_add_keyword(_("raster"));
    G_add_keyword(_("ISIS3"));
    module->label       = _("Export a GRASS raster map to an ISIS3 cube file.");
    module->description = _("Writes a GRASS raster to an ISIS3 .cub file using GDAL's "
                             "ISIS3 driver, then appends mission metadata keywords to the "
                             "PVL label (TargetName, InstrumentId, GRASS provenance).");

    opt_input = G_define_standard_option(G_OPT_R_INPUT);

    opt_output = G_define_option();
    opt_output->key         = "output";
    opt_output->type        = TYPE_STRING;
    opt_output->required    = YES;
    opt_output->description = _("Output ISIS3 cube file path (.cub)");
    opt_output->gisprompt   = "new_file,file,output";

    opt_type = G_define_option();
    opt_type->key         = "type";
    opt_type->type        = TYPE_STRING;
    opt_type->required    = NO;
    opt_type->answer      = "Float32";
    opt_type->options     = "Byte,Int16,UInt16,Int32,Float32,Float64";
    opt_type->description = _("Output data type (GDAL type name)");

    opt_target = G_define_option();
    opt_target->key         = "target";
    opt_target->type        = TYPE_STRING;
    opt_target->required    = NO;
    opt_target->description = _("Target body name (e.g. Mars, Moon) added to ISIS3 label");

    opt_inst = G_define_option();
    opt_inst->key         = "instrument";
    opt_inst->type        = TYPE_STRING;
    opt_inst->required    = NO;
    opt_inst->description = _("Instrument name added to ISIS3 label");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *input   = opt_input->answer;
    const char *output  = opt_output->answer;
    const char *dtype   = opt_type->answer;
    const char *target  = opt_target->answer;
    const char *inst    = opt_inst->answer;

    /* ---------------------------------------------------------------- */
    /* Check input map exists                                            */
    /* ---------------------------------------------------------------- */
    if (!G_find_raster((char *)input, ""))
        G_fatal_error(_("Raster map <%s> not found"), input);

    /* ---------------------------------------------------------------- */
    /* Export via r.out.gdal with ISIS3 driver                          */
    /* ---------------------------------------------------------------- */
    char gdal_cmd[4096];
    snprintf(gdal_cmd, sizeof(gdal_cmd),
             "r.out.gdal input=%s output=%s format=ISIS3 type=%s",
             input, output, dtype);

    G_message(_("Exporting via r.out.gdal (ISIS3 driver) ..."));
    int rc = system(gdal_cmd);
    if (rc != 0)
        G_fatal_error(_("r.out.gdal failed for '%s'"), input);

    /* ---------------------------------------------------------------- */
    /* Append metadata to the ISIS3 PVL label                           */
    /* (ISIS3 cubes produced by GDAL have an editable attached label)   */
    /* ---------------------------------------------------------------- */
    FILE *fp = fopen(output, "r+b");
    if (!fp) {
        G_warning(_("Cannot open '%s' to append metadata: %s"),
                   output, strerror(errno));
        goto done;
    }

    /* Read the existing label to find the END keyword position.
     * ISIS3 labels are null-padded to a fixed record size. */
    char label_buf[65536] = {0};
    size_t nr = fread(label_buf, 1, sizeof(label_buf)-1, fp);
    label_buf[nr] = '\0';

    /* Find "End\r\n" or "End\n" — the PVL terminator. */
    char *end_pos = strstr(label_buf, "\nEnd\r\n");
    if (!end_pos) end_pos = strstr(label_buf, "\nEnd\n");
    if (!end_pos) { fclose(fp); goto done; }

    /* Overwrite "End" with our new keywords + End. */
    long write_offset = (long)(end_pos + 1 - label_buf);
    fseek(fp, write_offset, SEEK_SET);

    /* Write metadata block. */
    char meta[2048];
    int meta_len = 0;

    if (target)
        meta_len += snprintf(meta + meta_len, sizeof(meta) - meta_len,
                             "TargetName = %s\r\n", target);
    if (inst)
        meta_len += snprintf(meta + meta_len, sizeof(meta) - meta_len,
                             "InstrumentId = %s\r\n", inst);

    /* Provenance: GRASS map history. */
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%jT%H:%M:%S", tm);
    meta_len += snprintf(meta + meta_len, sizeof(meta) - meta_len,
                         "/* Exported from GRASS raster <%s> by p.out.isis */\r\n"
                         "/* Export date: %s UTC */\r\n"
                         "End\r\n", input, timestr);

    fwrite(meta, 1, meta_len, fp);
    fclose(fp);

done:
    G_message(_("ISIS3 export complete: %s"), output);
    return EXIT_SUCCESS;
}
