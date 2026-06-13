/*!
 * \file test_p_meta.c
 * \brief Standalone unit tests for the p_meta C library.
 *
 * Compiled and run without a live GRASS session.  The real <grass/gis.h>
 * is replaced at compile time by test/grass/gis.h (stdlib redirects).
 * The GRASS mapset is emulated with a temporary directory tree.
 *
 * Build and run:
 *   cd libs/p_meta
 *   gcc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
 *       -I./test -o test_p_meta test_p_meta.c p_meta.c && ./test_p_meta
 *
 * (Unlicense – public domain dedication; SPDX-License-Identifier: Unlicense)
 */

#define _POSIX_C_SOURCE 200809L

#include "p_meta.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Test-fixture globals                                                 */
/* ------------------------------------------------------------------ */

static char g_tmpbase[512];  /* set once in main() via mkdtemp() */

/* Build "g_tmpbase/loc/PERMANENT/<subdir>/<mapname>" and create dirs. */
static void _mkdir_p(const char *path)
{
    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

static void setup_mapset(const char *mapname, const char *subdir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/%s/%s",
             g_tmpbase, subdir, mapname);
    _mkdir_p(path);
    setenv("GISDBASE",       g_tmpbase,  1);
    setenv("LOCATION_NAME",  "loc",      1);
    setenv("MAPSET",         "PERMANENT",1);
}

/* Return heap buffer with file contents (caller frees). Returns NULL if missing. */
static char *slurp(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    char *buf = (char *)malloc((size_t)len + 1);
    assert(buf);
    size_t got = fread(buf, 1, (size_t)len, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

/* Confirm s contains needle (for content assertions). */
static void assert_contains(const char *s, const char *needle, const char *test)
{
    if (!strstr(s, needle)) {
        fprintf(stderr, "FAIL [%s]: expected to find '%s' in:\n%s\n",
                test, needle, s);
        abort();
    }
}

static void assert_not_contains(const char *s, const char *needle, const char *test)
{
    if (strstr(s, needle)) {
        fprintf(stderr, "FAIL [%s]: expected NOT to find '%s' in:\n%s\n",
                test, needle, s);
        abort();
    }
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

static void test_new_free(void)
{
    PMeta *m = p_meta_new();
    assert(m != NULL);
    p_meta_free(m);
    p_meta_free(NULL);   /* must not crash */
    printf("PASS: test_new_free\n");
}

/* ------------------------------------------------------------------ */

static void test_write_basic(void)
{
    const char *mapname = "test_basic";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    p_meta_set_data_type(m, "image");
    p_meta_set_sensor(m, "ISS_NAC");
    p_meta_set_mission(m, "Cassini");
    p_meta_set_body(m, "SATURN");
    p_meta_set_radiometric_quantity(m, "raw_dn");
    p_meta_set_radiometric_units(m, "DN");
    p_meta_set_acquisition_datetime(m, "2004-07-01T03:11:40Z");
    p_meta_set_source_file(m, "/tmp/N1467345444.IMG");
    p_meta_set_pds_product_id(m, "N1467345444");
    p_meta_set_command(m, "p.in.rings input=raw output=test_basic");
    p_meta_set_n_bands(m, 1);

    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    /* Read JSON back and check key fields. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    assert_contains(json, "\"schema_version\": \"1.0\"",        "test_write_basic");
    assert_contains(json, "\"dataset_id\":",                     "test_write_basic");
    assert_contains(json, "\"derived\": false",                  "test_write_basic");
    assert_contains(json, "\"data_type\": \"image\"",            "test_write_basic");
    assert_contains(json, "\"sensor\": \"ISS_NAC\"",             "test_write_basic");
    assert_contains(json, "\"wavelength_units\": \"nm\"",        "test_write_basic");
    assert_contains(json, "\"radiometric_quantity\": \"raw_dn\"","test_write_basic");
    assert_contains(json, "\"radiometric_units\": \"DN\"",       "test_write_basic");
    assert_contains(json, "\"acquisition_datetime\": \"2004-07-01T03:11:40Z\"",
                                                                  "test_write_basic");
    assert_contains(json, "\"count\": 1",                        "test_write_basic");
    assert_contains(json, "\"body\": \"SATURN\"",                "test_write_basic");
    assert_contains(json, "\"mission\": \"Cassini\"",            "test_write_basic");
    assert_contains(json, "\"pds_product_id\": \"N1467345444\"", "test_write_basic");
    assert_contains(json, "\"source_file\": \"/tmp/N1467345444.IMG\"",
                                                                  "test_write_basic");
    assert_contains(json, "p.in.rings",                          "test_write_basic");

    free(json);
    printf("PASS: test_write_basic\n");
}

/* ------------------------------------------------------------------ */

static void test_write_3d(void)
{
    const char *mapname = "test_3d";
    setup_mapset(mapname, "grid3");

    PMeta *m = p_meta_new();
    p_meta_set_data_type(m, "dem");
    p_meta_set_body(m, "MOON");
    p_meta_set_n_bands(m, 1);
    int rc = p_meta_write_3d(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/grid3/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);
    assert_contains(json, "\"data_type\": \"dem\"", "test_write_3d");
    assert_contains(json, "\"body\": \"MOON\"",     "test_write_3d");
    free(json);
    printf("PASS: test_write_3d\n");
}

/* ------------------------------------------------------------------ */

static void test_first_write_wins(void)
{
    const char *mapname = "test_fww";
    setup_mapset(mapname, "cell_misc");

    PMeta *m1 = p_meta_new();
    p_meta_set_body(m1, "MARS");
    int rc = p_meta_write(m1, mapname);
    assert(rc == 0);
    p_meta_free(m1);

    /* Record mtime of first write. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    struct stat st1;
    stat(path, &st1);

    /* Second write with different body — must be silently skipped. */
    PMeta *m2 = p_meta_new();
    p_meta_set_body(m2, "VENUS");
    rc = p_meta_write(m2, mapname);
    assert(rc == 0);
    p_meta_free(m2);

    struct stat st2;
    stat(path, &st2);
    assert(st1.st_mtime == st2.st_mtime &&
           st1.st_size  == st2.st_size);

    char *json = slurp(path);
    assert(json != NULL);
    assert_contains(json,     "\"body\": \"MARS\"",   "test_first_write_wins");
    assert_not_contains(json, "VENUS",                 "test_first_write_wins");
    free(json);
    printf("PASS: test_first_write_wins\n");
}

/* ------------------------------------------------------------------ */

static void test_json_escaping(void)
{
    const char *mapname = "test_escape";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    /* Values that require escaping in JSON */
    p_meta_set_sensor(m, "CAM\"QUOTE");          /* embedded double-quote */
    p_meta_set_mission(m, "BACK\\SLASH");        /* backslash */
    p_meta_set_command(m, "cmd\nwith\nnewlines");/* embedded newlines */
    p_meta_set_body(m, "TAB\there");             /* embedded tab */

    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    /* The raw bytes in json must NOT contain unescaped control chars. */
    for (const unsigned char *p = (const unsigned char *)json; *p; p++) {
        if (*p < 0x20) {
            /* Only \n separating JSON lines is allowed in the structure;
             * values must be escaped. Check that none appear in string values
             * by verifying the escaped sequences are present. */
            assert(*p == '\n');  /* structural newlines only */
        }
    }

    /* Escaped sequences must appear verbatim in the JSON bytes. */
    assert_contains(json, "CAM\\\"QUOTE",    "test_json_escaping");
    assert_contains(json, "BACK\\\\SLASH",   "test_json_escaping");
    assert_contains(json, "\\n",             "test_json_escaping");
    assert_contains(json, "\\t",             "test_json_escaping");

    free(json);
    printf("PASS: test_json_escaping\n");
}

/* ------------------------------------------------------------------ */

static void test_wavelengths_fwhm(void)
{
    const char *mapname = "test_wl";
    setup_mapset(mapname, "cell_misc");

    static const double wl[]  = { 450.0, 550.0, 650.0 };
    static const double fw[]  = {  10.0,  12.0,  11.0 };

    PMeta *m = p_meta_new();
    p_meta_set_n_bands(m, 3);
    p_meta_set_wavelengths(m, wl, 3);
    p_meta_set_fwhm(m, fw, 3);

    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    assert_contains(json, "\"wavelength\":", "test_wavelengths_fwhm");
    assert_contains(json, "\"fwhm\":",       "test_wavelengths_fwhm");
    assert_contains(json, "450",             "test_wavelengths_fwhm");
    assert_contains(json, "550",             "test_wavelengths_fwhm");
    assert_contains(json, "650",             "test_wavelengths_fwhm");
    assert_contains(json, "\"count\": 3",    "test_wavelengths_fwhm");

    free(json);
    printf("PASS: test_wavelengths_fwhm\n");
}

/* ------------------------------------------------------------------ */

static void test_wavelengths_null(void)
{
    const char *mapname = "test_wl_null";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    p_meta_set_n_bands(m, 1);
    /* No wavelengths or fwhm set */
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    assert_not_contains(json, "\"wavelength\":", "test_wavelengths_null");
    assert_not_contains(json, "\"fwhm\":",       "test_wavelengths_null");
    assert_contains(json, "\"validity\": [true]", "test_wavelengths_null");

    free(json);
    printf("PASS: test_wavelengths_null\n");
}

/* ------------------------------------------------------------------ */

static void test_dataset_id_is_hex(void)
{
    const char *mapname = "test_uuid";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    /* Extract dataset_id value (expect 32 hex chars between the quotes). */
    const char *key = "\"dataset_id\": \"";
    const char *pos = strstr(json, key);
    assert(pos != NULL);
    pos += strlen(key);
    int hexlen = 0;
    while (isxdigit((unsigned char)pos[hexlen])) hexlen++;
    assert(hexlen == 32);
    assert(pos[hexlen] == '"');

    free(json);
    printf("PASS: test_dataset_id_is_hex\n");
}

/* ------------------------------------------------------------------ */

static void test_no_parent_dir_returns_error(void)
{
    /* map name whose cell_misc dir does NOT exist */
    const char *mapname = "no_such_map";
    setup_mapset("some_other_map", "cell_misc");  /* sets env vars */

    PMeta *m = p_meta_new();
    int rc = p_meta_write(m, mapname);
    assert(rc == -1);
    p_meta_free(m);
    printf("PASS: test_no_parent_dir_returns_error\n");
}

/* ------------------------------------------------------------------ */

static void test_null_sensor_emits_null(void)
{
    const char *mapname = "test_null_sensor";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    /* Leave sensor NULL — should emit "sensor": null */
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);
    assert_contains(json, "\"sensor\": null", "test_null_sensor_emits_null");
    free(json);
    printf("PASS: test_null_sensor_emits_null\n");
}

/* ------------------------------------------------------------------ */

static void test_empty_planetary_block(void)
{
    const char *mapname = "test_empty_planet";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    /* No body/mission/source — planetary block should be empty */
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);

    /* The planetary object should be empty: "planetary": {\n    }\n */
    assert_contains(json, "\"planetary\": {", "test_empty_planetary_block");
    assert_not_contains(json, "\"body\":",    "test_empty_planetary_block");
    assert_not_contains(json, "\"mission\":", "test_empty_planetary_block");

    free(json);
    printf("PASS: test_empty_planetary_block\n");
}

/* ------------------------------------------------------------------ */

static void test_set_clear_wavelengths(void)
{
    PMeta *m = p_meta_new();
    static const double wl[] = {450.0, 550.0};
    p_meta_set_wavelengths(m, wl, 2);
    /* Clear by passing NULL */
    p_meta_set_wavelengths(m, NULL, 0);

    const char *mapname = "test_clear_wl";
    setup_mapset(mapname, "cell_misc");
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);
    assert_not_contains(json, "\"wavelength\":", "test_set_clear_wavelengths");
    free(json);
    printf("PASS: test_set_clear_wavelengths\n");
}

/* ------------------------------------------------------------------ */

static void test_processing_history_command(void)
{
    const char *mapname = "test_history";
    setup_mapset(mapname, "cell_misc");

    PMeta *m = p_meta_new();
    p_meta_set_command(m, "p.in.pds3 input=foo.img output=test_history");
    int rc = p_meta_write(m, mapname);
    assert(rc == 0);
    p_meta_free(m);

    char path[1024];
    snprintf(path, sizeof(path), "%s/loc/PERMANENT/cell_misc/%s/planetary.json",
             g_tmpbase, mapname);
    char *json = slurp(path);
    assert(json != NULL);
    assert_contains(json, "\"processing_history\":", "test_processing_history_command");
    assert_contains(json, "p.in.pds3",               "test_processing_history_command");
    assert_contains(json, "\"timestamp\":",           "test_processing_history_command");
    assert_contains(json, "\"inputs\": []",           "test_processing_history_command");
    assert_contains(json, "\"outputs\": []",          "test_processing_history_command");
    free(json);
    printf("PASS: test_processing_history_command\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* Create a unique temp base directory for all test mapsets. */
    char tmpdir_tmpl[] = "/tmp/test_p_meta_XXXXXX";
    char *tmpdir = mkdtemp(tmpdir_tmpl);
    assert(tmpdir != NULL);
    strncpy(g_tmpbase, tmpdir, sizeof(g_tmpbase) - 1);

    printf("=== p_meta C unit tests ===\n");
    printf("tmp: %s\n", g_tmpbase);

    test_new_free();
    test_write_basic();
    test_write_3d();
    test_first_write_wins();
    test_json_escaping();
    test_wavelengths_fwhm();
    test_wavelengths_null();
    test_dataset_id_is_hex();
    test_no_parent_dir_returns_error();
    test_null_sensor_emits_null();
    test_empty_planetary_block();
    test_set_clear_wavelengths();
    test_processing_history_command();

    /* Clean up */
    char rm_cmd[600];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", g_tmpbase);
    system(rm_cmd);

    printf("=== ALL PASSED ===\n");
    return 0;
}
