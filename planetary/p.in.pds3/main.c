/****************************************************************************
 *
 * MODULE:       p.in.pds3
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Import a PDS3 planetary image (IMAGE, QUBE, SPECTRAL_QUBE)
 *               into one or more GRASS raster maps.
 *
 *               Single-band products create one raster named <output>.
 *               Multi-band products create <output>.1, <output>.2, …
 *               and optionally register them in a GRASS imagery group (-g).
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

/* p_pds library (compiled in) */
#include "../../libs/p_pds/p_pds.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */
static void write_band(PPdsImage *img, int band,
                        const char *mapname, int null_special,
                        const char *title);

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Flag    *flag_group, *flag_null;


    /* ---- GRASS init ---- */
    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Import & Export"));
    G_add_keyword(_("import"));
    G_add_keyword(_("raster"));
    G_add_keyword(_("PDS3"));
    module->label       = _("Import a PDS3 planetary image into GRASS raster map(s).");
    module->description = _("Reads a PDS3 IMAGE, QUBE or SPECTRAL_QUBE product and writes "
                             "one GRASS raster per band.  Multi-band cubes produce maps named "
                             "output.1, output.2, etc.  Optionally registers the maps in "
                             "a GRASS imagery group with the -g flag.");

    opt_input = G_define_option();
    opt_input->key         = "input";
    opt_input->type        = TYPE_STRING;
    opt_input->required    = YES;
    opt_input->description = _("Path to the PDS3 label file (.lbl or combined .img/.img)");
    opt_input->gisprompt   = "old_file,file,input";

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Name for output raster map (band suffix added for multi-band)");

    flag_group = G_define_flag();
    flag_group->key         = 'g';
    flag_group->description = _("Register output maps in a GRASS imagery group");

    flag_null = G_define_flag();
    flag_null->key         = 'n';
    flag_null->description = _("Map ISIS/PDS CORE_NULL DN to GRASS NULL (default: map to NULL)");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *input   = opt_input->answer;
    const char *outbase = opt_output->answer;
    int use_group  = flag_group->answer;
    int null_special = 1;  /* always map special DNs; flag_null only controls user message */

    /* ---------------------------------------------------------------- */
    /* Open the PDS3 product                                              */
    /* ---------------------------------------------------------------- */
    G_message(_("Opening PDS3 product: %s"), input);
    PPdsImage *img = p_pds_open_image(input);
    if (!img)
        G_fatal_error(_("Cannot open PDS3 product '%s'"), input);

    int nlines  = img->lines;
    int nsamples = img->samples;
    int nbands   = img->bands;

    G_message(_("  Dimensions: %d lines x %d samples x %d band(s)"),
               nlines, nsamples, nbands);
    G_message(_("  OFFSET=%.6g  SCALING_FACTOR=%.6g"),
               img->offset, img->scaling_factor);

    /* ---------------------------------------------------------------- */
    /* Set / verify computational region to match the PDS image          */
    /* ---------------------------------------------------------------- */
    struct Cell_head region;
    Rast_get_window(&region);

    /* If no region has been set yet (default region), initialise from image. */
    if (region.rows == 0 || region.cols == 0 ||
        (region.rows == 1 && region.cols == 1)) {
        region.rows  = nlines;
        region.cols  = nsamples;
        region.north = nlines;
        region.south = 0.0;
        region.east  = nsamples;
        region.west  = 0.0;
        region.ns_res = 1.0;
        region.ew_res = 1.0;
        Rast_set_window(&region);
        G_message(_("  Region set to %d x %d pixels (pixel coordinates)."),
                   nlines, nsamples);
    }

    if (region.rows != nlines || region.cols != nsamples) {
        G_warning(_("Current region (%d x %d) does not match PDS image "
                    "(%d x %d). Import will be clipped/padded to region."),
                   region.rows, region.cols, nlines, nsamples);
    }

    /* ---------------------------------------------------------------- */
    /* Build band map names                                               */
    /* ---------------------------------------------------------------- */
    char **mapnames = (char **)G_malloc((size_t)nbands * sizeof(char *));
    for (int b = 0; b < nbands; b++) {
        mapnames[b] = (char *)G_malloc(256);
        if (nbands == 1)
            snprintf(mapnames[b], 256, "%s", outbase);
        else
            snprintf(mapnames[b], 256, "%s.%d", outbase, b + 1);
    }

    /* ---------------------------------------------------------------- */
    /* Write each band                                                    */
    /* ---------------------------------------------------------------- */
    for (int b = 0; b < nbands; b++) {
        char title[512];
        if (nbands == 1)
            snprintf(title, sizeof(title), "PDS3 %s", outbase);
        else
            snprintf(title, sizeof(title), "PDS3 %s band %d/%d",
                     outbase, b + 1, nbands);

        G_message(_("  Writing band %d/%d → %s"), b + 1, nbands, mapnames[b]);
        write_band(img, b, mapnames[b], null_special, title);
    }

    /* ---------------------------------------------------------------- */
    /* Optionally create imagery group                                    */
    /* ---------------------------------------------------------------- */
    if (use_group && nbands > 1) {
        /* Call i.group via G_spawn (simplest portable approach). */
        char group_cmd[1024];
        snprintf(group_cmd, sizeof(group_cmd),
                 "i.group group=%s subgroup=%s input=", outbase, outbase);
        for (int b = 0; b < nbands; b++) {
            strncat(group_cmd, mapnames[b], sizeof(group_cmd) - strlen(group_cmd) - 2);
            if (b < nbands - 1)
                strncat(group_cmd, ",", sizeof(group_cmd) - strlen(group_cmd) - 2);
        }
        if (system(group_cmd) != 0)
            G_warning(_("i.group call failed — group '%s' not created"), outbase);
        else
            G_message(_("Imagery group '%s' created."), outbase);
    }

    /* ---------------------------------------------------------------- */
    /* Cleanup                                                            */
    /* ---------------------------------------------------------------- */
    p_pds_close(img);
    for (int b = 0; b < nbands; b++) G_free(mapnames[b]);
    G_free(mapnames);

    G_message(_("Done. %d band(s) imported as '%s%s'."),
               nbands, outbase, nbands > 1 ? ".N" : "");
    return EXIT_SUCCESS;
}

/* ================================================================== */
/* write_band: read one PDS3 band, write it as a GRASS DCELL raster   */
/* ================================================================== */
static void write_band(PPdsImage *img, int band,
                        const char *mapname, int null_special,
                        const char *title)
{
    int nrows    = img->lines;
    int ncols    = img->samples;
    int out_rows = Rast_window_rows();
    int out_cols = Rast_window_cols();

    /* Open output raster. */
    int outfd = Rast_open_new(mapname, DCELL_TYPE);

    /* Allocate row buffers. */
    double  *pds_row = (double *)G_malloc((size_t)ncols * sizeof(double));
    DCELL   *out_row = Rast_allocate_d_buf();

    for (int row = 0; row < out_rows; row++) {
        G_percent(row, out_rows, 2);

        if (row < nrows) {
            /* Read from PDS. */
            if (p_pds_read_row(img, band, row, pds_row, null_special) != 0) {
                Rast_set_d_null_value(out_row, out_cols);
            } else {
                for (int col = 0; col < out_cols; col++) {
                    if (col < ncols) {
                        double v = pds_row[col];
                        if (v != v) { /* NaN → GRASS NULL */
                            Rast_set_d_null_value(&out_row[col], 1);
                        } else {
                            out_row[col] = (DCELL)v;
                        }
                    } else {
                        Rast_set_d_null_value(&out_row[col], 1);
                    }
                }
            }
        } else {
            Rast_set_d_null_value(out_row, out_cols);
        }

        Rast_put_d_row(outfd, out_row);
    }
    G_percent(1, 1, 2);

    Rast_close(outfd);
    G_free(pds_row);
    G_free(out_row);

    /* Write map title and history. */
    if (title && *title) {
        struct Categories cats;
        Rast_init_cats(title, &cats);
        Rast_write_cats(mapname, &cats);
        Rast_free_cats(&cats);
    }

    struct History hist;
    Rast_short_history(mapname, "raster", &hist);
    Rast_command_history(&hist);
    Rast_write_history(mapname, &hist);
}
