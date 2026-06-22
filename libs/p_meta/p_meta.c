/*!
 * \file p_meta.c
 * \brief Planetary map metadata — C implementation.
 *
 * Writes planetary.json compatible with the i.hyper HyperMetadata JSON schema
 * (GRASS grass-addons i_hyper_lib/hyper_meta.py).  No external JSON library
 * is required; the file is serialised with a lightweight hand-written emitter.
 *
 * \author Yann Chemin
 * \copyright The Unlicense (public domain)
 */

#include "p_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#include <grass/gis.h>

/* ------------------------------------------------------------------ */
/* Schema constants                                                     */
/* ------------------------------------------------------------------ */
#define P_META_SCHEMA_VERSION "1.0"
#define P_META_FILENAME       "planetary.json"

/* ------------------------------------------------------------------ */
/* Internal struct                                                      */
/* ------------------------------------------------------------------ */
struct PMeta {
    char *data_type;
    char *sensor;
    char *mission;
    char *body;
    char *acquisition_datetime;
    char *radiometric_quantity;
    char *radiometric_units;
    char *wavelength_units;
    int   n_bands;
    char *source_file;
    char *pds_product_id;
    char *command;

    double *wavelengths;
    int     n_wavelengths;
    double *fwhm;
    int     n_fwhm;
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static char *_strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char *)G_malloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

static void _set_str(char **dst, const char *src)
{
    if (*dst) G_free(*dst);
    *dst = _strdup_safe(src);
}

/* ------------------------------------------------------------------ */
/* UUID generation (hex, no hyphens, 32 chars)                         */
/* ------------------------------------------------------------------ */
static void _make_uuid(char out[33])
{
    unsigned char buf[16] = {0};
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) {
        (void)fread(buf, 1, 16, fp);
        fclose(fp);
        /* Set version 4 and variant bits */
        buf[6] = (buf[6] & 0x0f) | 0x40;
        buf[8] = (buf[8] & 0x3f) | 0x80;
    } else {
        /* Fallback: mix of time + address bits */
        unsigned long t = (unsigned long)time(NULL);
        for (int i = 0; i < 16; i++) {
            buf[i] = (unsigned char)(t >> (i % 8) ^ (unsigned long)out >> ((i * 3) % 8));
            t = t * 6364136223846793005ULL + 1442695040888963407ULL;
        }
    }
    for (int i = 0; i < 16; i++)
        snprintf(out + i*2, 3, "%02x", buf[i]);
    out[32] = '\0';
}

/* ------------------------------------------------------------------ */
/* ISO 8601 timestamp (current UTC)                                     */
/* ------------------------------------------------------------------ */
static void _iso_now(char out[32])
{
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", tm);
}

/* ------------------------------------------------------------------ */
/* JSON string escaper                                                  */
/* Writes the escaped form of s into fp, surrounded by double quotes.  */
/* ------------------------------------------------------------------ */
static void _json_str(FILE *fp, const char *s)
{
    fputc('"', fp);
    if (!s) { fputc('"', fp); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\n': fputs("\\n",  fp); break;
        case '\r': fputs("\\r",  fp); break;
        case '\t': fputs("\\t",  fp); break;
        default:
            if (*p < 0x20)
                fprintf(fp, "\\u%04x", *p);
            else
                fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

/* ------------------------------------------------------------------ */
/* GRASS mapset path                                                    */
/* Returns heap-allocated "gisdbase/location/mapset"; caller frees.    */
/* ------------------------------------------------------------------ */
static char *_mapset_path(void)
{
    const char *gisdbase = G_getenv("GISDBASE");
    const char *location = G_getenv("LOCATION_NAME");
    const char *mapset   = G_getenv("MAPSET");

    if (!gisdbase || !location || !mapset) return NULL;

    size_t len = strlen(gisdbase) + 1 + strlen(location) + 1 + strlen(mapset) + 1;
    char *path = (char *)G_malloc(len);
    snprintf(path, len, "%s/%s/%s", gisdbase, location, mapset);
    return path;
}

/* ------------------------------------------------------------------ */
/* Core writer                                                          */
/* ------------------------------------------------------------------ */
static int _write_json(PMeta *m, const char *dir_path, const char *mapname)
{
    /* Build full path: dir_path/mapname/planetary.json */
    size_t dlen = strlen(dir_path) + 1 + strlen(mapname) + 1
                  + strlen(P_META_FILENAME) + 1;
    char *path = (char *)G_malloc(dlen);
    snprintf(path, dlen, "%s/%s/%s", dir_path, mapname, P_META_FILENAME);

    /* Skip if already present (first-write wins). */
    struct stat st;
    if (stat(path, &st) == 0) {
        G_verbose_message("p_meta: %s already exists, skipping.", path);
        G_free(path);
        return 0;
    }

    /* Verify parent directory exists. */
    char *parent = (char *)G_malloc(strlen(dir_path) + 1 + strlen(mapname) + 1);
    snprintf(parent, strlen(dir_path) + 1 + strlen(mapname) + 1,
             "%s/%s", dir_path, mapname);
    if (stat(parent, &st) != 0 || !S_ISDIR(st.st_mode)) {
        G_warning("p_meta: directory '%s' not found; "
                  "planetary.json not written for '%s'.", parent, mapname);
        G_free(parent);
        G_free(path);
        return -1;
    }
    G_free(parent);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        G_warning("p_meta: cannot open '%s' for writing: %s",
                  path, strerror(errno));
        G_free(path);
        return -1;
    }

    /* UUIDs and timestamps */
    char uuid[33];  _make_uuid(uuid);
    char now[32];   _iso_now(now);

    int n = (m->n_bands > 0) ? m->n_bands : 1;

    /* ---- Write JSON ---- */
    fputs("{\n", fp);
    fprintf(fp, "  \"schema_version\": \"%s\",\n", P_META_SCHEMA_VERSION);
    fprintf(fp, "  \"dataset_id\": \"%s\",\n", uuid);
    fputs("  \"derived\": false,\n", fp);

    /* data_type */
    fputs("  \"data_type\": ", fp);
    _json_str(fp, m->data_type ? m->data_type : "image");
    fputs(",\n", fp);

    /* sensor */
    fputs("  \"sensor\": ", fp);
    if (m->sensor) { _json_str(fp, m->sensor); fputs(",\n", fp); }
    else            fputs("null,\n", fp);

    /* wavelength_units */
    fputs("  \"wavelength_units\": ", fp);
    _json_str(fp, m->wavelength_units ? m->wavelength_units : "nm");
    fputs(",\n", fp);

    /* radiometric_quantity */
    fputs("  \"radiometric_quantity\": ", fp);
    if (m->radiometric_quantity) { _json_str(fp, m->radiometric_quantity); fputs(",\n", fp); }
    else                          fputs("null,\n", fp);

    /* radiometric_units */
    fputs("  \"radiometric_units\": ", fp);
    if (m->radiometric_units) { _json_str(fp, m->radiometric_units); fputs(",\n", fp); }
    else                       fputs("null,\n", fp);

    /* acquisition_datetime */
    fputs("  \"acquisition_datetime\": ", fp);
    if (m->acquisition_datetime) { _json_str(fp, m->acquisition_datetime); fputs(",\n", fp); }
    else                          fputs("null,\n", fp);

    /* bands */
    fputs("  \"bands\": {\n", fp);
    fprintf(fp, "    \"count\": %d,\n", n);
    fprintf(fp, "    \"count_valid\": %d,\n", n);

    /* wavelengths (optional) */
    if (m->wavelengths && m->n_wavelengths > 0) {
        fputs("    \"wavelength\": [", fp);
        for (int i = 0; i < m->n_wavelengths; i++) {
            if (i > 0) fputc(',', fp);
            fprintf(fp, "%.4g", m->wavelengths[i]);
        }
        fputs("],\n", fp);
    }

    /* fwhm (optional) */
    if (m->fwhm && m->n_fwhm > 0) {
        fputs("    \"fwhm\": [", fp);
        for (int i = 0; i < m->n_fwhm; i++) {
            if (i > 0) fputc(',', fp);
            fprintf(fp, "%.4g", m->fwhm[i]);
        }
        fputs("],\n", fp);
    }

    /* validity: all true */
    fputs("    \"validity\": [", fp);
    for (int i = 0; i < n; i++) {
        if (i > 0) fputc(',', fp);
        fputs("true", fp);
    }
    fputs("]\n", fp);   /* last item in bands — no trailing comma */
    fputs("  },\n", fp);

    /* processing_history */
    fputs("  \"processing_history\": [\n", fp);
    fputs("    {\n", fp);
    fputs("      \"command\": ", fp);
    _json_str(fp, m->command ? m->command : "");
    fputs(",\n", fp);
    fprintf(fp, "      \"timestamp\": \"%s\",\n", now);
    fputs("      \"inputs\": [],\n", fp);
    fputs("      \"outputs\": []\n", fp);
    fputs("    }\n", fp);
    fputs("  ],\n", fp);

    /* extended_metadata.planetary */
    fputs("  \"extended_metadata\": {\n", fp);
    fputs("    \"planetary\": {\n", fp);

    int first_planetary = 1;

#define _PMETA_KV(key, val) \
    do { \
        if (val) { \
            if (!first_planetary) fputs(",\n", fp); \
            fputs("      \"" key "\": ", fp); \
            _json_str(fp, val); \
            first_planetary = 0; \
        } \
    } while (0)

    _PMETA_KV("body",           m->body);
    _PMETA_KV("mission",        m->mission);
    _PMETA_KV("pds_product_id", m->pds_product_id);
    _PMETA_KV("source_file",    m->source_file);

#undef _PMETA_KV

    if (!first_planetary) fputc('\n', fp);
    fputs("    }\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);

    fclose(fp);
    G_verbose_message("p_meta: wrote %s", path);
    G_free(path);
    return 0;
}

/* ================================================================== */
/* Public API                                                          */
/* ================================================================== */

PMeta *p_meta_new(void)
{
    PMeta *m = (PMeta *)G_calloc(1, sizeof(PMeta));
    m->n_bands = 1;
    return m;
}

void p_meta_free(PMeta *m)
{
    if (!m) return;
    G_free(m->data_type);
    G_free(m->sensor);
    G_free(m->mission);
    G_free(m->body);
    G_free(m->acquisition_datetime);
    G_free(m->radiometric_quantity);
    G_free(m->radiometric_units);
    G_free(m->wavelength_units);
    G_free(m->source_file);
    G_free(m->pds_product_id);
    G_free(m->command);
    G_free(m->wavelengths);
    G_free(m->fwhm);
    G_free(m);
}

void p_meta_set_data_type(PMeta *m, const char *v)            { _set_str(&m->data_type,             v); }
void p_meta_set_sensor(PMeta *m, const char *v)               { _set_str(&m->sensor,                 v); }
void p_meta_set_mission(PMeta *m, const char *v)              { _set_str(&m->mission,                v); }
void p_meta_set_body(PMeta *m, const char *v)                 { _set_str(&m->body,                   v); }
void p_meta_set_acquisition_datetime(PMeta *m, const char *v) { _set_str(&m->acquisition_datetime,   v); }
void p_meta_set_radiometric_quantity(PMeta *m, const char *v) { _set_str(&m->radiometric_quantity,   v); }
void p_meta_set_radiometric_units(PMeta *m, const char *v)    { _set_str(&m->radiometric_units,      v); }
void p_meta_set_source_file(PMeta *m, const char *v)          { _set_str(&m->source_file,            v); }
void p_meta_set_pds_product_id(PMeta *m, const char *v)       { _set_str(&m->pds_product_id,         v); }
void p_meta_set_command(PMeta *m, const char *v)              { _set_str(&m->command,                v); }

void p_meta_set_n_bands(PMeta *m, int n)
{
    m->n_bands = (n > 0) ? n : 1;
}

void p_meta_set_wavelengths(PMeta *m, const double *wl, int n)
{
    G_free(m->wavelengths);
    m->wavelengths   = NULL;
    m->n_wavelengths = 0;
    if (!wl || n <= 0) return;
    m->wavelengths = (double *)G_malloc((size_t)n * sizeof(double));
    memcpy(m->wavelengths, wl, (size_t)n * sizeof(double));
    m->n_wavelengths = n;
}

void p_meta_set_fwhm(PMeta *m, const double *fwhm, int n)
{
    G_free(m->fwhm);
    m->fwhm   = NULL;
    m->n_fwhm = 0;
    if (!fwhm || n <= 0) return;
    m->fwhm = (double *)G_malloc((size_t)n * sizeof(double));
    memcpy(m->fwhm, fwhm, (size_t)n * sizeof(double));
    m->n_fwhm = n;
}

/* ------------------------------------------------------------------ */
/* p_meta_install_matter_bands                                          */
/* ------------------------------------------------------------------ */

#define _P_META_MATTER_BANDS "matter_bands.json"

int p_meta_install_matter_bands(void)
{
    /* Source: $GISBASE/etc/planetary/matter_bands.json */
    const char *gisbase = G_getenv_nofatal("GISBASE");
    if (!gisbase) return -1;

    const char *rel = "/etc/planetary/" _P_META_MATTER_BANDS;
    size_t src_len = strlen(gisbase) + strlen(rel) + 1;
    char *src = (char *)G_malloc(src_len);
    snprintf(src, src_len, "%s%s", gisbase, rel);

    struct stat st;
    if (stat(src, &st) != 0) {
        /* Source absent — development build not installed yet; not an error. */
        G_verbose_message(
            "p_meta: matter_bands.json not found at '%s'; skipping Misc install.",
            src);
        G_free(src);
        return 0;
    }

    /* Destination: $MAPSET/Misc/matter_bands.json */
    char *ms = _mapset_path();
    if (!ms) { G_free(src); return -1; }

    size_t misc_len = strlen(ms) + sizeof("/Misc");
    char *misc_dir = (char *)G_malloc(misc_len);
    snprintf(misc_dir, misc_len, "%s/Misc", ms);
    G_free(ms);

    /* Create Misc/ if absent. */
    if (stat(misc_dir, &st) != 0) {
        if (mkdir(misc_dir, 0755) != 0 && errno != EEXIST) {
            G_warning("p_meta: cannot create '%s': %s",
                      misc_dir, strerror(errno));
            G_free(misc_dir);
            G_free(src);
            return -1;
        }
    }

    size_t dst_len = strlen(misc_dir) + 1 + strlen(_P_META_MATTER_BANDS) + 1;
    char *dst = (char *)G_malloc(dst_len);
    snprintf(dst, dst_len, "%s/%s", misc_dir, _P_META_MATTER_BANDS);
    G_free(misc_dir);

    /* First-write wins — idempotent across every band import. */
    if (stat(dst, &st) == 0) {
        G_verbose_message("p_meta: %s already present, skipping.", dst);
        G_free(dst);
        G_free(src);
        return 0;
    }

    /* Copy src → dst with a local buffer; no external deps needed. */
    FILE *fin  = fopen(src, "rb");
    FILE *fout = fopen(dst, "wb");
    if (!fin || !fout) {
        G_warning("p_meta: cannot copy matter_bands.json to '%s': %s",
                  dst, strerror(errno));
        if (fin)  fclose(fin);
        if (fout) fclose(fout);
        G_free(dst);
        G_free(src);
        return -1;
    }

    char buf[8192];
    size_t nr;
    while ((nr = fread(buf, 1, sizeof(buf), fin)) > 0)
        fwrite(buf, 1, nr, fout);

    fclose(fin);
    fclose(fout);
    G_verbose_message("p_meta: installed %s → %s", src, dst);
    G_free(src);
    G_free(dst);
    return 0;
}

/* ------------------------------------------------------------------ */
/* p_meta_write — 2-D raster (cell_misc)                               */
/* ------------------------------------------------------------------ */
int p_meta_write(PMeta *m, const char *mapname)
{
    char *ms = _mapset_path();
    if (!ms) {
        G_warning("p_meta: cannot determine GRASS mapset path.");
        return -1;
    }

    size_t len = strlen(ms) + sizeof("/cell_misc");
    char *dir = (char *)G_malloc(len);
    snprintf(dir, len, "%s/cell_misc", ms);
    G_free(ms);

    int rc = _write_json(m, dir, mapname);
    G_free(dir);

    p_meta_install_matter_bands();
    return rc;
}

/* ------------------------------------------------------------------ */
/* p_meta_read_string_field — minimal targeted JSON field reader       */
/* ------------------------------------------------------------------ */

/*
 * Find the next double-quoted string after needle in buf and copy its
 * unescaped contents into out (caller-allocated, outlen bytes). Minimal
 * unescaping (\", \\, \n, \r, \t, \uXXXX -> raw byte) -- a mirror of
 * _json_str()'s escaper above, not a general JSON parser. Returns 0 on
 * success, -1 if no quoted string follows needle.
 */
static int _json_extract_after(const char *buf, const char *needle,
                                char *out, int outlen)
{
    const char *p = strstr(buf, needle);
    if (!p) return -1;
    p += strlen(needle);
    /* Skip to the opening quote of the value (past ':' and whitespace). */
    while (*p && *p != '"') {
        if (*p == ',' || *p == '}') return -1; /* hit next field/end first */
        p++;
    }
    if (*p != '"') return -1;
    p++;

    int n = 0;
    while (*p && *p != '"' && n < outlen - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            case '"': out[n++] = '"';  break;
            case '\\': out[n++] = '\\'; break;
            case 'u':
                /* \uXXXX -> skip 4 hex digits, emit '?' (field values used
                 * by this reader -- sensor ids -- are plain ASCII; this
                 * just avoids corrupting the scan position). */
                out[n++] = '?';
                for (int i = 0; i < 4 && p[1]; i++) p++;
                break;
            default: out[n++] = *p;
            }
            p++;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    return (*p == '"') ? 0 : -1;
}

int p_meta_read_string_field(const char *mapname, const char *map_type,
                              const char *field, char *out, int outlen)
{
    if (!mapname || !field || !out || outlen < 1) return -1;

    char *ms = _mapset_path();
    if (!ms) return -1;

    const char *subdir = (map_type && strcmp(map_type, "raster3d") == 0)
                              ? "grid3" : "cell_misc";
    size_t len = strlen(ms) + strlen(subdir) + strlen(mapname) +
                 strlen(P_META_FILENAME) + 8;
    char *path = (char *)G_malloc(len);
    snprintf(path, len, "%s/%s/%s/%s", ms, subdir, mapname, P_META_FILENAME);
    G_free(ms);

    FILE *fp = fopen(path, "rb");
    G_free(path);
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 1 << 20) { fclose(fp); return -1; }

    char *buf = (char *)G_malloc((size_t)size + 1);
    size_t nread = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[nread] = '\0';

    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    int rc = _json_extract_after(buf, needle, out, outlen);
    G_free(buf);
    return rc;
}

/* ------------------------------------------------------------------ */
/* p_meta_write_3d — 3-D raster (grid3)                                */
/* ------------------------------------------------------------------ */
int p_meta_write_3d(PMeta *m, const char *mapname)
{
    char *ms = _mapset_path();
    if (!ms) {
        G_warning("p_meta: cannot determine GRASS mapset path.");
        return -1;
    }

    size_t len = strlen(ms) + sizeof("/grid3");
    char *dir = (char *)G_malloc(len);
    snprintf(dir, len, "%s/grid3", ms);
    G_free(ms);

    int rc = _write_json(m, dir, mapname);
    G_free(dir);

    p_meta_install_matter_bands();
    return rc;
}
