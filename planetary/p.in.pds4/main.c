/****************************************************************************
 *
 * MODULE:       p.in.pds4
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Import a PDS4 planetary data product into GRASS raster map(s).
 *
 *               Handles Array_2D_Image and Array_3D_Image products.
 *               The PDS4 label is an XML file; the binary data file is
 *               referenced by the File_Area_Observational element.
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
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <libgen.h>   /* dirname() */

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

/* libxml2 for PDS4 XML parsing. */
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

/* ------------------------------------------------------------------ */
/* Byte-swap helper                                                     */
/* ------------------------------------------------------------------ */
static int is_little_endian(void)
{
    union { uint32_t w; uint8_t b[4]; } u;
    u.w = 1;
    return u.b[0];
}

static void swap_bytes(void *buf, int n, int sz)
{
    uint8_t *p = (uint8_t *)buf;
    for (int i = 0; i < n; i++, p += sz)
        for (int j = 0; j < sz/2; j++) {
            uint8_t t = p[j]; p[j] = p[sz-1-j]; p[sz-1-j] = t;
        }
}

/* ------------------------------------------------------------------ */
/* XPath text helper                                                    */
/* ------------------------------------------------------------------ */
static char *xpath_text(xmlDocPtr doc, xmlXPathContextPtr ctx,
                         const char *expr)
{
    xmlXPathObjectPtr obj = xmlXPathEvalExpression((xmlChar *)expr, ctx);
    if (!obj) return NULL;
    char *result = NULL;
    if (obj->nodesetval && obj->nodesetval->nodeNr > 0) {
        xmlChar *s = xmlNodeGetContent(obj->nodesetval->nodeTab[0]);
        if (s) {
            /* Strip whitespace */
            char *t = (char *)s;
            while (*t && isspace((unsigned char)*t)) t++;
            int len = (int)strlen(t);
            while (len > 0 && isspace((unsigned char)t[len-1])) len--;
            result = G_malloc(len + 1);
            memcpy(result, t, len);
            result[len] = '\0';
            xmlFree(s);
        }
    }
    xmlXPathFreeObject(obj);
    return result;
}

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output;
    struct Flag    *flag_group;
    struct History  history;

    G_gisinit(argv[0]);

    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Import & Export"));
    G_add_keyword(_("import"));
    G_add_keyword(_("raster"));
    G_add_keyword(_("PDS4"));
    module->label       = _("Import a PDS4 planetary data product into GRASS raster map(s).");
    module->description = _("Reads a PDS4 XML label and its referenced binary data file "
                             "(Array_2D_Image or Array_3D_Image) and writes GRASS raster maps. "
                             "Multi-band products create output.1, output.2, etc.");

    opt_input = G_define_option();
    opt_input->key         = "input";
    opt_input->type        = TYPE_STRING;
    opt_input->required    = YES;
    opt_input->description = _("Path to the PDS4 XML label file (.xml)");
    opt_input->gisprompt   = "old_file,file,input";

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->description = _("Name for output raster map");

    flag_group = G_define_flag();
    flag_group->key         = 'g';
    flag_group->description = _("Register output maps in a GRASS imagery group");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    const char *xml_path = opt_input->answer;
    const char *outbase  = opt_output->answer;

    /* ---------------------------------------------------------------- */
    /* Parse PDS4 XML label                                              */
    /* ---------------------------------------------------------------- */
    xmlInitParser();
    LIBXML_TEST_VERSION;

    xmlDocPtr doc = xmlParseFile(xml_path);
    if (!doc)
        G_fatal_error(_("Cannot parse XML label '%s'"), xml_path);

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) {
        xmlFreeDoc(doc);
        G_fatal_error(_("Cannot create XPath context"));
    }

    /* Register the PDS4 namespace so XPath can address elements. */
    xmlXPathRegisterNs(ctx, (xmlChar *)"pds",
        (xmlChar *)"http://pds.nasa.gov/pds4/pds/v1");

    /* ---- Locate data file name ---- */
    char *data_fname = xpath_text(doc, ctx,
        "//pds:File_Area_Observational/pds:File/pds:file_name");
    if (!data_fname)
        data_fname = xpath_text(doc, ctx,
            "//*[local-name()='file_name']");
    if (!data_fname)
        G_fatal_error(_("Cannot find file_name in PDS4 label '%s'"), xml_path);

    /* Build full path to data file (same directory as label). */
    char label_dir[4096];
    strncpy(label_dir, xml_path, sizeof(label_dir)-1);
    label_dir[sizeof(label_dir)-1] = '\0';
    char *dir = dirname(label_dir);
    char data_path[4096];
    snprintf(data_path, sizeof(data_path), "%s/%s", dir, data_fname);
    G_free(data_fname);

    /* ---- Image dimensions ---- */
    char *lines_str   = xpath_text(doc, ctx, "//*[local-name()='lines']");
    char *samples_str = xpath_text(doc, ctx, "//*[local-name()='samples']");
    char *bands_str   = xpath_text(doc, ctx, "//*[local-name()='bands']");
    char *dtype_str   = xpath_text(doc, ctx,
        "//*[local-name()='Element_Array']/*[local-name()='data_type']");
    char *offset_str  = xpath_text(doc, ctx,
        "//*[local-name()='offset']");

    int nlines   = lines_str   ? atoi(lines_str)   : 0;
    int nsamples = samples_str ? atoi(samples_str) : 0;
    int nbands   = bands_str   ? atoi(bands_str)   : 1;
    long data_offset = offset_str ? atol(offset_str) : 0;

    if (nlines <= 0 || nsamples <= 0)
        G_fatal_error(_("Could not determine image dimensions from '%s'"), xml_path);

    /* ---- Data type ---- */
    /* PDS4 data type names: IEEE754MSBSingle, IEEE754MSBDouble,
     *   SignedMSB2, UnsignedMSB2, UnsignedByte, etc. */
    int bytes_per_pixel = 4;
    int is_float = 1;
    int is_msb   = 1;

    if (dtype_str) {
        if (strstr(dtype_str, "Double") || strstr(dtype_str, "64"))
            bytes_per_pixel = 8;
        else if (strstr(dtype_str, "Byte") || strstr(dtype_str, "1"))
            bytes_per_pixel = 1;
        else if (strstr(dtype_str, "2") && !strstr(dtype_str, "32"))
            bytes_per_pixel = 2;
        else
            bytes_per_pixel = 4;

        if (strstr(dtype_str, "Integer") || strstr(dtype_str, "MSB2") ||
            strstr(dtype_str, "LSB2") || strstr(dtype_str, "Byte"))
            is_float = 0;

        if (strstr(dtype_str, "LSB"))
            is_msb = 0;
    }

    G_message(_("PDS4: %d x %d x %d bands, dtype=%s, offset=%ld"),
               nlines, nsamples, nbands,
               dtype_str ? dtype_str : "unknown", data_offset);

    /* Free XPath resources */
    G_free(lines_str); G_free(samples_str); G_free(bands_str);
    G_free(dtype_str); G_free(offset_str);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    /* ---------------------------------------------------------------- */
    /* Set region to image if not already set                            */
    /* ---------------------------------------------------------------- */
    struct Cell_head region;
    G_get_window(&region);
    if (region.rows == 0 || region.cols == 0) {
        region.rows  = nlines;  region.cols  = nsamples;
        region.north = nlines;  region.south = 0.0;
        region.east  = nsamples; region.west = 0.0;
        region.ns_res = region.ew_res = 1.0;
        G_set_window(&region);
    }

    /* ---------------------------------------------------------------- */
    /* Open binary data file and write bands                             */
    /* ---------------------------------------------------------------- */
    FILE *fp = fopen(data_path, "rb");
    if (!fp)
        G_fatal_error(_("Cannot open data file '%s': %s"),
                       data_path, strerror(errno));

    int need_swap = (is_msb == is_little_endian()) && bytes_per_pixel > 1;
    int out_rows  = region.rows;
    int out_cols  = region.cols;

    uint8_t *raw = (uint8_t *)G_malloc((size_t)nsamples * bytes_per_pixel);
    DCELL   *outbuf = Rast_allocate_d_buf();

    char **mapnames = (char **)G_malloc((size_t)nbands * sizeof(char *));
    for (int b = 0; b < nbands; b++) {
        mapnames[b] = (char *)G_malloc(256);
        if (nbands == 1)
            snprintf(mapnames[b], 256, "%s", outbase);
        else
            snprintf(mapnames[b], 256, "%s.%d", outbase, b+1);
    }

    /* BSQ layout assumed (Array_3D default). */
    long band_size = (long)nlines * nsamples * bytes_per_pixel;

    for (int b = 0; b < nbands; b++) {
        G_message(_("  Writing band %d/%d → %s"), b+1, nbands, mapnames[b]);
        int outfd = Rast_open_new(mapnames[b], DCELL_TYPE);

        for (int row = 0; row < out_rows; row++) {
            G_percent(row, out_rows, 2);
            if (row < nlines) {
                long pos = data_offset
                           + (long)b * band_size
                           + (long)row * nsamples * bytes_per_pixel;
                fseek(fp, pos, SEEK_SET);
                int got = (int)fread(raw, bytes_per_pixel, nsamples, fp);
                if (need_swap && got > 0)
                    swap_bytes(raw, got, bytes_per_pixel);

                for (int col = 0; col < out_cols; col++) {
                    if (col >= nsamples || col >= got) {
                        Rast_set_d_null_value(&outbuf[col], 1);
                        continue;
                    }
                    double v;
                    void *ptr = raw + (size_t)col * bytes_per_pixel;
                    if (is_float) {
                        if (bytes_per_pixel == 8) memcpy(&v, ptr, 8);
                        else { float f; memcpy(&f, ptr, 4); v = f; }
                    } else {
                        switch (bytes_per_pixel) {
                        case 1: v = *((uint8_t *)ptr); break;
                        case 2: v = *((int16_t *)ptr); break;
                        case 4: v = *((int32_t *)ptr); break;
                        default: v = 0.0;
                        }
                    }
                    if (v != v) Rast_set_d_null_value(&outbuf[col], 1);
                    else        outbuf[col] = (DCELL)v;
                }
            } else {
                Rast_set_d_null_value(outbuf, out_cols);
            }
            Rast_put_d_row(outfd, outbuf);
        }
        G_percent(1, 1, 2);
        Rast_close(outfd);

        struct History hist;
        Rast_short_history(mapnames[b], "raster", &hist);
        Rast_command_history(&hist);
        Rast_write_history(mapnames[b], &hist);
    }

    fclose(fp);
    G_free(raw); G_free(outbuf);

    /* Optional group */
    if (flag_group->answer && nbands > 1) {
        char group_cmd[2048];
        snprintf(group_cmd, sizeof(group_cmd),
                 "i.group group=%s subgroup=%s input=", outbase, outbase);
        for (int b = 0; b < nbands; b++) {
            strncat(group_cmd, mapnames[b], sizeof(group_cmd)-strlen(group_cmd)-2);
            if (b < nbands-1) strncat(group_cmd, ",", 2);
        }
        system(group_cmd);
    }

    for (int b = 0; b < nbands; b++) G_free(mapnames[b]);
    G_free(mapnames);

    G_message(_("Done. %d band(s) imported from PDS4 product."), nbands);
    return EXIT_SUCCESS;
}
