/*!
 * \file test_p_pds.c
 *
 * \brief Unit test for p_pds library.
 *
 * Compiled standalone (no GRASS mapset needed) via:
 *   gcc -std=c99 -D_POSIX_C_SOURCE=200809L -DP_PDS_STANDALONE \
 *       -o test_p_pds test_p_pds.c p_pds.c -lm && ./test_p_pds
 *
 * (Unlicense - public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _POSIX_C_SOURCE 200809L

#include "p_pds.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* PVL parsing tests                                                    */
/* ------------------------------------------------------------------ */

static void test_pvl_scalar(void)
{
    const char *pvl_text =
        "PDS_VERSION_ID = PDS3\n"
        "RECORD_BYTES   = 40000\n"
        "LABEL_RECORDS  = 1\n"
        "PI_VALUE       = 3.14159 <rad>\n"
        "END\n";

    FILE *fp = fmemopen((void *)pvl_text, strlen(pvl_text), "r");
    assert(fp);

    PPvlNode *root = p_pvl_parse("test_inline", fp);
    fclose(fp);
    assert(root);

    const char *ver = p_pvl_value(root, "PDS_VERSION_ID");
    assert(ver && strcmp(ver, "PDS3") == 0);

    int ok;
    int rb = p_pvl_value_int(root, "RECORD_BYTES", &ok);
    assert(ok && rb == 40000);

    double pi = p_pvl_value_double(root, "PI_VALUE", &ok);
    assert(ok && fabs(pi - 3.14159) < 1e-5);

    p_pvl_free(root);
    printf("PASS: test_pvl_scalar\n");
}

static void test_pvl_object(void)
{
    const char *pvl_text =
        "PDS_VERSION_ID = PDS3\n"
        "OBJECT = IMAGE\n"
        "  LINES        = 512\n"
        "  LINE_SAMPLES = 1024\n"
        "  SAMPLE_BITS  = 16\n"
        "  SAMPLE_TYPE  = MSB_UNSIGNED_INTEGER\n"
        "END_OBJECT = IMAGE\n"
        "END\n";

    FILE *fp = fmemopen((void *)pvl_text, strlen(pvl_text), "r");
    PPvlNode *root = p_pvl_parse("test_inline", fp);
    fclose(fp);
    assert(root);

    PPvlNode *img = p_pvl_find_object(root, "IMAGE");
    assert(img);

    int ok;
    int lines = p_pvl_value_int(img, "LINES", &ok);
    assert(ok && lines == 512);
    int samps = p_pvl_value_int(img, "LINE_SAMPLES", &ok);
    assert(ok && samps == 1024);

    const char *st = p_pvl_value(img, "SAMPLE_TYPE");
    assert(st && strcmp(st, "MSB_UNSIGNED_INTEGER") == 0);

    p_pvl_free(root);
    printf("PASS: test_pvl_object\n");
}

static void test_pvl_nested(void)
{
    const char *pvl_text =
        "OBJECT = TABLE\n"
        "  ROWS    = 10\n"
        "  OBJECT = COLUMN\n"
        "    NAME  = J2000X\n"
        "    BYTES = 8\n"
        "  END_OBJECT = COLUMN\n"
        "END_OBJECT = TABLE\n"
        "END\n";

    FILE *fp = fmemopen((void *)pvl_text, strlen(pvl_text), "r");
    PPvlNode *root = p_pvl_parse("test_inline", fp);
    fclose(fp);
    assert(root);

    PPvlNode *tbl = p_pvl_find_object(root, "TABLE");
    assert(tbl);
    PPvlNode *col = p_pvl_find_object(tbl, "COLUMN");
    assert(col);
    const char *name = p_pvl_value(col, "NAME");
    assert(name && strcmp(name, "J2000X") == 0);

    p_pvl_free(root);
    printf("PASS: test_pvl_nested\n");
}

/* ------------------------------------------------------------------ */
/* Byte-swap tests                                                      */
/* ------------------------------------------------------------------ */

static void test_byte_swap(void)
{
    uint16_t v16 = 0x0102;
    p_pds_swap_bytes(&v16, 1, 2);
    assert(v16 == 0x0201);

    uint32_t v32 = 0x01020304u;
    p_pds_swap_bytes(&v32, 1, 4);
    assert(v32 == 0x04030201u);

    printf("PASS: test_byte_swap\n");
}

/* ------------------------------------------------------------------ */
/* Synthetic image read test                                            */
/* Construct a minimal PDS3 IMAGE label + 8-bit pixel data in memory.  */
/* ------------------------------------------------------------------ */

static void test_synthetic_image(void)
{
    /* Build a tiny 4x4, 1-band, 8-bit MSB_UNSIGNED_INTEGER image.
     * Pixel values are 0..15 (row-major BSQ). */
    static const uint8_t pixels[16] = {
         0,  1,  2,  3,
         4,  5,  6,  7,
         8,  9, 10, 11,
        12, 13, 14, 15
    };

    /* Write a temporary PDS3 file (label + attached data). */
    char tmppath[] = "/tmp/test_p_pds_XXXXXX";
    int fd = mkstemp(tmppath);
    assert(fd >= 0);
    FILE *fp = fdopen(fd, "w+b");
    assert(fp);

    /* PVL label — fixed-length, 1 record of 512 bytes. */
    const char *label_fmt =
        "PDS_VERSION_ID = PDS3\r\n"
        "RECORD_TYPE    = FIXED_LENGTH\r\n"
        "RECORD_BYTES   = 512\r\n"
        "FILE_RECORDS   = 2\r\n"
        "LABEL_RECORDS  = 1\r\n"
        "^IMAGE         = 2\r\n"
        "OBJECT = IMAGE\r\n"
        "  LINES        = 4\r\n"
        "  LINE_SAMPLES = 4\r\n"
        "  BANDS        = 1\r\n"
        "  SAMPLE_BITS  = 8\r\n"
        "  SAMPLE_TYPE  = MSB_UNSIGNED_INTEGER\r\n"
        "  OFFSET       = 0.0\r\n"
        "  SCALING_FACTOR = 1.0\r\n"
        "  CORE_NULL    = 0\r\n"
        "END_OBJECT = IMAGE\r\n"
        "END\r\n";

    /* Write the label padded to 512 bytes. */
    size_t llen = strlen(label_fmt);
    fwrite(label_fmt, 1, llen, fp);
    for (size_t i = llen; i < 512; i++) fputc(' ', fp);

    /* Write pixel data padded to 512 bytes. */
    fwrite(pixels, 1, 16, fp);
    for (int i = 16; i < 512; i++) fputc(0, fp);
    fflush(fp);
    fclose(fp);

    /* Now open with the library. */
    PPdsImage *img = p_pds_open_image(tmppath);
    assert(img);
    assert(img->lines   == 4);
    assert(img->samples == 4);
    assert(img->bands   == 1);
    assert(img->dtype   == P_PDS_DTYPE_UINT8);

    double row_buf[4];

    /* Row 0: expect 0, 1, 2, 3 */
    assert(p_pds_read_row(img, 0, 0, row_buf, 0) == 0);
    for (int s = 0; s < 4; s++)
        assert(fabs(row_buf[s] - s) < 1e-9);

    /* Row 2: expect 8, 9, 10, 11 */
    assert(p_pds_read_row(img, 0, 2, row_buf, 0) == 0);
    for (int s = 0; s < 4; s++)
        assert(fabs(row_buf[s] - (8 + s)) < 1e-9);

    p_pds_close(img);
    remove(tmppath);

    printf("PASS: test_synthetic_image\n");
}

static void test_named_object_selection(void)
{
    /* Two sibling image objects in one attached label (mirrors the real
     * M3 L1B convention: RDN_FILE/RDN_IMAGE and LOC_FILE/LOC_IMAGE side
     * by side, each with its own ^xxx_IMAGE pointer). Default selection
     * (p_pds_open_image) must pick the first (RDN_IMAGE, values 0..15);
     * explicit selection (p_pds_open_image_named) must pick LOC_IMAGE
     * (values 100..115) instead. */
    /* Pixel byte values are kept above 0x7E so scan_past_ascii's
     * stale-pointer heuristic (which treats printable ASCII as still
     * being inside label text) never mistakes real pixel data for
     * label text and shifts the read offset. */
    static const uint8_t rdn_pixels[16] = {
        200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215
    };
    static const uint8_t loc_pixels[16] = {
        220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235
    };

    char tmppath[] = "/tmp/test_p_pds_named_XXXXXX";
    int fd = mkstemp(tmppath);
    assert(fd >= 0);
    FILE *fp = fdopen(fd, "w+b");
    assert(fp);

    /* Use explicit <BYTES> pointers computed from the real label length,
     * rather than RECORD_BYTES-based record numbers -- this label is
     * longer than any one fixed record size would conveniently be, and
     * <BYTES> offsets sidestep that arithmetic entirely. */
    const char *label_tmpl =
        "PDS_VERSION_ID = PDS3\r\n"
        "RECORD_TYPE    = UNDEFINED\r\n"
        "OBJECT = RDN_FILE\r\n"
        "  ^RDN_IMAGE   = %06ld <BYTES>\r\n"
        "  OBJECT = RDN_IMAGE\r\n"
        "    LINES        = 4\r\n"
        "    LINE_SAMPLES = 4\r\n"
        "    BANDS        = 1\r\n"
        "    SAMPLE_BITS  = 8\r\n"
        "    SAMPLE_TYPE  = MSB_UNSIGNED_INTEGER\r\n"
        "    OFFSET       = 0.0\r\n"
        "    SCALING_FACTOR = 1.0\r\n"
        "    CORE_NULL    = 0\r\n"
        "  END_OBJECT = RDN_IMAGE\r\n"
        "END_OBJECT = RDN_FILE\r\n"
        "OBJECT = LOC_FILE\r\n"
        "  ^LOC_IMAGE   = %06ld <BYTES>\r\n"
        "  OBJECT = LOC_IMAGE\r\n"
        "    LINES        = 4\r\n"
        "    LINE_SAMPLES = 4\r\n"
        "    BANDS        = 1\r\n"
        "    SAMPLE_BITS  = 8\r\n"
        "    SAMPLE_TYPE  = MSB_UNSIGNED_INTEGER\r\n"
        "    OFFSET       = 0.0\r\n"
        "    SCALING_FACTOR = 1.0\r\n"
        "    CORE_NULL    = 0\r\n"
        "  END_OBJECT = LOC_IMAGE\r\n"
        "END_OBJECT = LOC_FILE\r\n"
        "END\r\n";

    /* %06ld keeps a fixed 6-digit field width regardless of the offset's
     * actual value, so a first pass with dummy offsets already tells us
     * the real serialized length -- no manual length arithmetic needed. */
    char label_buf[2048];
    int label_len = snprintf(label_buf, sizeof(label_buf), label_tmpl, 0L, 0L);
    assert(label_len > 0 && (size_t)label_len < sizeof(label_buf));

    long rdn_off = label_len;
    long loc_off = label_len + 16;
    int label_len2 = snprintf(label_buf, sizeof(label_buf), label_tmpl,
                               rdn_off, loc_off);
    assert(label_len2 == label_len); /* offsets must format to the same width */

    fwrite(label_buf, 1, (size_t)label_len, fp);
    fwrite(rdn_pixels, 1, 16, fp);
    fwrite(loc_pixels, 1, 16, fp);
    fflush(fp);
    fclose(fp);

    double row_buf[4];

    PPdsImage *img_default = p_pds_open_image(tmppath);
    assert(img_default);
    assert(p_pds_read_row(img_default, 0, 0, row_buf, 0) == 0);
    for (int s = 0; s < 4; s++)
        assert(fabs(row_buf[s] - (200 + s)) < 1e-9);
    p_pds_close(img_default);

    PPdsImage *img_loc = p_pds_open_image_named(tmppath, "LOC_IMAGE");
    assert(img_loc);
    assert(p_pds_read_row(img_loc, 0, 0, row_buf, 0) == 0);
    for (int s = 0; s < 4; s++)
        assert(fabs(row_buf[s] - (220 + s)) < 1e-9);
    p_pds_close(img_loc);

    PPdsImage *img_missing = p_pds_open_image_named(tmppath, "OBS_IMAGE");
    assert(img_missing == NULL);

    remove(tmppath);
    printf("PASS: test_named_object_selection\n");
}

/* ------------------------------------------------------------------ */
/* ISIS3 test-data label (detached, no .img needed — label parse only) */
/* ------------------------------------------------------------------ */

static void test_hirise_label_parse(void)
{
    const char *lbl_path =
        "/home/yann/dev/ISIS3/isis/src/base/objs/ProcessImportPds/"
        "data/pdsImageWithTables.lbl";

    FILE *fp = fopen(lbl_path, "r");
    if (!fp) {
        printf("SKIP: test_hirise_label_parse (test data not available)\n");
        return;
    }

    PPvlNode *root = p_pvl_parse(lbl_path, fp);
    fclose(fp);
    assert(root);

    /* Spot-check top-level keywords. */
    const char *ver = p_pvl_value(root, "PDS_VERSION_ID");
    assert(ver && strcmp(ver, "PDS3") == 0);

    int ok;
    int rb = p_pvl_value_int(root, "RECORD_BYTES", &ok);
    assert(ok && rb == 40000);

    /* IMAGE object. */
    PPvlNode *img = p_pvl_find_object(root, "IMAGE");
    assert(img);

    int lines = p_pvl_value_int(img, "LINES", &ok);
    assert(ok && lines == 10);
    int samps = p_pvl_value_int(img, "LINE_SAMPLES", &ok);
    assert(ok && samps == 20000);
    int bands = p_pvl_value_int(img, "BANDS", &ok);
    assert(!ok || bands == 1); /* BANDS not present → default 1 */

    int bits = p_pvl_value_int(img, "SAMPLE_BITS", &ok);
    assert(ok && bits == 16);

    const char *st = p_pvl_value(img, "SAMPLE_TYPE");
    assert(st && strcmp(st, "MSB_UNSIGNED_INTEGER") == 0);

    double sf = p_pvl_value_double(img, "SCALING_FACTOR", &ok);
    assert(ok && fabs(sf - 1.32153376269238e-06) < 1e-18);

    /* SUN_POSITION_TABLE object. */
    PPvlNode *tbl = p_pvl_find_object(root, "SUN_POSITION_TABLE");
    assert(tbl);
    int rows = p_pvl_value_int(tbl, "ROWS", &ok);
    assert(ok && rows == 2);

    /* First column nested inside table. */
    PPvlNode *col = p_pvl_find_object(tbl, "COLUMN");
    assert(col);
    const char *cname = p_pvl_value(col, "NAME");
    assert(cname && strcmp(cname, "J2000X") == 0);

    p_pvl_free(root);
    printf("PASS: test_hirise_label_parse\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== p_pds unit tests ===\n");
    test_pvl_scalar();
    test_pvl_object();
    test_pvl_nested();
    test_byte_swap();
    test_synthetic_image();
    test_named_object_selection();
    test_hirise_label_parse();
    printf("=== ALL PASSED ===\n");
    return 0;
}
