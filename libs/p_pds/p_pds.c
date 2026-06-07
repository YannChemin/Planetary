/*!
 * \file p_pds.c
 *
 * \brief Planetary library - PDS3/PDS4 label and image I/O (implementation).
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_pds.h"

#ifdef _OPENMP
#  include <omp.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* When compiled standalone (e.g. for unit tests without a GRASS install),
 * define P_PDS_STANDALONE to substitute minimal malloc/free/warning stubs. */
#ifdef P_PDS_STANDALONE
#  include <stdarg.h>
static void *G_malloc(size_t n) { void *p = malloc(n); return p; }
static void *G_calloc(size_t n, size_t s) { return calloc(n, s); }
static void *G_realloc(void *p, size_t n) { return realloc(p, n); }
static void  G_free(void *p) { free(p); }
static void  G_warning(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "WARNING: "); vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
}
static void  G_message(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
}
#  define _(s) (s)
#else
#  include <grass/gis.h>
#  include <grass/glocale.h>
#endif

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

#define P_PDS_LINE_MAX 4096

/* Strip leading and trailing ASCII whitespace from s in-place. */
static void strip_ws(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

/* Upper-case copy (returned pointer is static — copy before next call). */
static char *str_upper(const char *s)
{
    static char buf[256];
    int i;
    for (i = 0; s[i] && i < 255; i++)
        buf[i] = (char)toupper((unsigned char)s[i]);
    buf[i] = '\0';
    return buf;
}

/* Case-insensitive strcmp. */
static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* Strip surrounding double- or single-quotes from value string in-place. */
static void strip_quotes(char *s)
{
    int n = (int)strlen(s);
    if (n >= 2 &&
        ((s[0] == '"' && s[n-1] == '"') ||
         (s[0] == '\'' && s[n-1] == '\''))) {
        memmove(s, s + 1, (size_t)(n - 2));
        s[n - 2] = '\0';
    }
}

/* Allocate and return a heap copy of s. */
static char *heap_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)G_malloc(n);
    memcpy(p, s, n);
    return p;
}

/* New empty PVL node. */
static PPvlNode *pvl_new_node(void)
{
    PPvlNode *n = (PPvlNode *)G_calloc(1, sizeof(PPvlNode));
    return n;
}

/* ================================================================== */
/* PVL parser                                                           */
/* ================================================================== */

/*
 * Line-oriented PVL parser.  PDS3 PVL grammar summary:
 *   keyword = value            (scalar)
 *   keyword = (v1, v2, …)     (sequence — stored as raw string)
 *   keyword = {v1, v2, …}     (set — stored as raw string)
 *   OBJECT = name … END_OBJECT = name
 *   GROUP  = name … END_GROUP  = name
 *   END                        (terminates the label)
 *
 * Continuation lines (ending with '-' are NOT in PDS3; multi-line values
 * use implicit line-continuation inside balanced parentheses).
 */

/* Forward declaration for recursive parsing. */
static PPvlNode *pvl_parse_block(FILE *fp, const char *path,
                                  const char *end_kw, const char *end_val);

/* Parse one complete PVL value that may span multiple lines
 * (e.g. a multi-line quoted string or a parenthesised sequence).
 * Returns heap string.  fp is positioned just after the '='. */
static char *pvl_read_value(FILE *fp, const char *line_rest)
{
    /* Accumulate into a growable buffer. */
    size_t cap = 256, len = 0;
    char  *buf = (char *)G_malloc(cap);
    buf[0] = '\0';

    /* Copy rest of first line into buf. */
    size_t rlen = strlen(line_rest);
    if (len + rlen + 1 > cap) {
        cap = (len + rlen + 1) * 2;
        buf = (char *)G_realloc(buf, cap);
    }
    memcpy(buf + len, line_rest, rlen + 1);
    len += rlen;

    /* Count unmatched '(' — we need to read continuation lines
     * until parentheses are balanced. */
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '(') depth++;
        else if (buf[i] == ')') depth--;
    }

    char line[P_PDS_LINE_MAX];
    while (depth > 0 && fgets(line, sizeof(line), fp)) {
        /* Strip newline. */
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
            line[--ll] = '\0';

        /* Append a space + this line. */
        if (len + ll + 2 > cap) {
            cap = (len + ll + 2) * 2;
            buf = (char *)G_realloc(buf, cap);
        }
        buf[len++] = ' ';
        memcpy(buf + len, line, ll + 1);
        len += ll;

        for (size_t i = 0; i < ll; i++) {
            if (line[i] == '(') depth++;
            else if (line[i] == ')') depth--;
        }
    }

    strip_ws(buf);
    return buf;
}

/* Parse a PVL block until end_kw = end_val (or END / EOF).
 * Returns linked list of sibling PPvlNode. */
static PPvlNode *pvl_parse_block(FILE *fp, const char *path,
                                  const char *end_kw, const char *end_val)
{
    PPvlNode *head = NULL, *tail = NULL;
    char line[P_PDS_LINE_MAX];

    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline and carriage-return. */
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
            line[--ll] = '\0';

        strip_ws(line);

        /* Skip empty lines and PVL comment blocks (slash-star syntax).
         * PDS3 PVL rarely uses them but we skip lines starting with '/'. */
        if (line[0] == '\0' || line[0] == '/')
            continue;

        /* "END" terminates the entire label. */
        if (str_ieq(line, "END"))
            break;

        /* END_OBJECT / END_GROUP may appear without a '=' value.
         * Check before splitting, so they aren't silently skipped. */
        {
            char up[32];
            int k;
            for (k = 0; line[k] && k < 31 && line[k] != ' ' && line[k] != '='; k++)
                up[k] = (char)toupper((unsigned char)line[k]);
            up[k] = '\0';
            if (end_kw && (str_ieq(up, "END_OBJECT") || str_ieq(up, "END_GROUP")))
                break;
        }

        /* Split on first '='. */
        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key_raw = line;
        char *val_raw = eq + 1;
        strip_ws(key_raw);
        strip_ws(val_raw);

        char *key = str_upper(key_raw);

        /* Check for END_OBJECT / END_GROUP with a value (e.g. END_OBJECT = IMAGE). */
        if (end_kw && (str_ieq(key, "END_OBJECT") || str_ieq(key, "END_GROUP"))) {
            (void)end_val;
            break;
        }

        PPvlNode *node = pvl_new_node();
        node->key = heap_str(key);

        if (str_ieq(key, "OBJECT") || str_ieq(key, "GROUP")) {
            node->type  = str_ieq(key, "OBJECT") ? P_PVL_OBJECT : P_PVL_GROUP;
            strip_ws(val_raw);
            node->value = heap_str(val_raw);
            /* Recurse into this block. */
            node->children = pvl_parse_block(fp, path,
                                              str_ieq(key,"OBJECT") ? "END_OBJECT" : "END_GROUP",
                                              val_raw);
        }
        else {
            node->type  = P_PVL_SCALAR;
            /* Read possibly multi-line value. */
            char *full_val = pvl_read_value(fp, val_raw);

            /* Extract unit from <unit> if present: "value <unit>". */
            char *unit_start = strchr(full_val, '<');
            if (unit_start) {
                char *unit_end = strchr(unit_start, '>');
                if (unit_end) {
                    *unit_end = '\0';
                    node->unit  = heap_str(unit_start + 1);
                    *unit_start = '\0';
                    strip_ws(full_val);
                }
            }

            strip_quotes(full_val);
            strip_ws(full_val);
            node->value = full_val; /* transfer ownership */
        }

        /* Append to sibling list. */
        if (!head) {
            head = tail = node;
        }
        else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

PPvlNode *p_pvl_parse(const char *path, void *fp)
{
    PPvlNode *root = pvl_new_node();
    root->type     = P_PVL_OBJECT;
    root->key      = heap_str("ROOT");
    root->value    = heap_str("");
    root->children = pvl_parse_block((FILE *)fp, path, NULL, NULL);
    return root;
}

/* ================================================================== */
/* PVL query helpers                                                    */
/* ================================================================== */

PPvlNode *p_pvl_find(const PPvlNode *parent, const char *key)
{
    if (!parent) return NULL;
    PPvlNode *n = parent->children;
    while (n) {
        if (n->type == P_PVL_SCALAR && str_ieq(n->key, key))
            return n;
        n = n->next;
    }
    return NULL;
}

PPvlNode *p_pvl_find_object(const PPvlNode *parent, const char *name)
{
    if (!parent) return NULL;
    PPvlNode *n = parent->children;
    while (n) {
        if ((n->type == P_PVL_OBJECT || n->type == P_PVL_GROUP)
            && n->value && str_ieq(n->value, name))
            return n;
        n = n->next;
    }
    return NULL;
}

/* Recursive depth-first search for a named OBJECT/GROUP. */
static PPvlNode *pvl_find_object_deep(const PPvlNode *parent, const char *name)
{
    if (!parent) return NULL;
    PPvlNode *hit = p_pvl_find_object(parent, name);
    if (hit) return hit;
    /* Recurse into each child OBJECT/GROUP. */
    PPvlNode *n = parent->children;
    while (n) {
        if (n->type == P_PVL_OBJECT || n->type == P_PVL_GROUP) {
            hit = pvl_find_object_deep(n, name);
            if (hit) return hit;
        }
        n = n->next;
    }
    return NULL;
}

const char *p_pvl_value(const PPvlNode *parent, const char *key)
{
    PPvlNode *n = p_pvl_find(parent, key);
    return n ? n->value : NULL;
}

double p_pvl_value_double(const PPvlNode *parent, const char *key, int *ok)
{
    const char *v = p_pvl_value(parent, key);
    if (!v) { if (ok) *ok = 0; return 0.0; }
    char *end;
    double d = strtod(v, &end);
    if (end == v) { if (ok) *ok = 0; return 0.0; }
    if (ok) *ok = 1;
    return d;
}

int p_pvl_value_int(const PPvlNode *parent, const char *key, int *ok)
{
    const char *v = p_pvl_value(parent, key);
    if (!v) { if (ok) *ok = 0; return 0; }
    char *end;
    long l = strtol(v, &end, 0);
    if (end == v) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    return (int)l;
}

void p_pvl_free(PPvlNode *root)
{
    if (!root) return;
    p_pvl_free(root->children);
    p_pvl_free(root->next);
    G_free(root->key);
    G_free(root->value);
    G_free(root->unit);
    G_free(root);
}

/* ================================================================== */
/* Byte-order utilities                                                 */
/* ================================================================== */

int p_pds_is_little_endian(void)
{
    union { uint32_t w; uint8_t b[4]; } u;
    u.w = 1;
    return u.b[0] == 1;
}

void p_pds_swap_bytes(void *buf, int n, int elem_size)
{
    uint8_t *p = (uint8_t *)buf;
    int i, j;
    for (i = 0; i < n; i++, p += elem_size) {
        for (j = 0; j < elem_size / 2; j++) {
            uint8_t tmp       = p[j];
            p[j]              = p[elem_size - 1 - j];
            p[elem_size-1-j]  = tmp;
        }
    }
}

/* ================================================================== */
/* SAMPLE_TYPE string → dtype + is_msb                                 */
/* ================================================================== */

/*
 * PDS3 standard SAMPLE_TYPE values (PDS Standards Reference Table 12-3).
 * We handle the most common; exotic types (VAX_REAL, etc.) produce
 * P_PDS_DTYPE_UNKNOWN and a warning.
 */
static void parse_sample_type(const char *st, int bits,
                               PPdsDataType *dtype, int *is_msb)
{
    char up[64];
    int i;
    for (i = 0; st[i] && i < 63; i++)
        up[i] = (char)toupper((unsigned char)st[i]);
    up[i] = '\0';

    *is_msb = 1; /* PDS default is MSB */

    /* Unsigned integers */
    if (strstr(up, "UNSIGNED") || str_ieq(up, "UNSIGNED_INTEGER")) {
        if (strstr(up, "LSB")) *is_msb = 0;
        switch (bits) {
        case 8:  *dtype = P_PDS_DTYPE_UINT8;  break;
        case 16: *dtype = P_PDS_DTYPE_UINT16; break;
        case 32: *dtype = P_PDS_DTYPE_UINT32; break;
        default: *dtype = P_PDS_DTYPE_UNKNOWN;
        }
        return;
    }

    /* Signed integers */
    if (strstr(up, "INTEGER") || str_ieq(up, "SIGNED_INTEGER")) {
        if (strstr(up, "LSB")) *is_msb = 0;
        switch (bits) {
        case 8:  *dtype = P_PDS_DTYPE_UINT8;  break; /* PDS "BYTE" is unsigned */
        case 16: *dtype = P_PDS_DTYPE_INT16;  break;
        case 32: *dtype = P_PDS_DTYPE_INT32;  break;
        default: *dtype = P_PDS_DTYPE_UNKNOWN;
        }
        return;
    }

    /* Floating point: IEEE_REAL (MSB) or PC_REAL (LSB) */
    if (str_ieq(up, "IEEE_REAL") || str_ieq(up, "REAL") ||
        str_ieq(up, "FLOAT") || str_ieq(up, "SUN_REAL")) {
        *is_msb = 1;
        switch (bits) {
        case 32: *dtype = P_PDS_DTYPE_FLOAT32; break;
        case 64: *dtype = P_PDS_DTYPE_FLOAT64; break;
        default: *dtype = P_PDS_DTYPE_UNKNOWN;
        }
        return;
    }
    if (str_ieq(up, "PC_REAL")) {
        *is_msb = 0;
        switch (bits) {
        case 32: *dtype = P_PDS_DTYPE_FLOAT32; break;
        case 64: *dtype = P_PDS_DTYPE_FLOAT64; break;
        default: *dtype = P_PDS_DTYPE_UNKNOWN;
        }
        return;
    }

    /* Raw byte / unsigned byte */
    if (str_ieq(up, "UNSIGNED_BYTE") || str_ieq(up, "BYTE")) {
        *dtype = P_PDS_DTYPE_UINT8;
        return;
    }

    G_warning(_("p_pds: unknown SAMPLE_TYPE '%s', assuming UINT8"), st);
    *dtype  = P_PDS_DTYPE_UINT8;
    *is_msb = 1;
}

/* ================================================================== */
/* For a detached .lbl label, find the companion binary data file     */
/* by trying common data extensions in order.  Returns a heap copy    */
/* of the full path if found, NULL otherwise.                         */
/* ================================================================== */
static char *find_companion_data_file(const char *label_path)
{
    static const char *data_exts[] = {
        ".img", ".IMG", ".dat", ".DAT", ".fit", ".FIT",
        ".qub", ".QUB", NULL
    };

    /* Build basename (without extension) */
    char base[2048];
    strncpy(base, label_path, sizeof(base) - 10);
    base[sizeof(base) - 10] = '\0';
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0'; /* strip label extension */

    char candidate[2048];
    for (int i = 0; data_exts[i]; i++) {
        snprintf(candidate, sizeof(candidate), "%s%s", base, data_exts[i]);
        FILE *f = fopen(candidate, "rb");
        if (f) {
            fclose(f);
            return heap_str(candidate);
        }
    }
    return NULL;
}

/* Returns 1 if path has a label-file extension (.lbl, .LBL, .label, .LABEL). */
static int has_label_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    return (str_ieq(dot + 1, "lbl") || str_ieq(dot + 1, "label"));
}

/* ================================================================== */
/* Locate ^IMAGE / ^QUBE pointer and resolve data file path            */
/* ================================================================== */

/*
 * PDS3 §4.3 data-pointer keyword.
 * Value may be:
 *   "filename.img"              → detached file, offset 0
 *   ("filename.img", 42 <BYTES>) → detached, byte offset
 *   42                          → attached, byte offset
 *   ("filename.img", 42)        → detached, record offset (needs RECORD_BYTES)
 */
static int resolve_data_pointer(PPvlNode *root, const char *label_path,
                                 const char *object_name,
                                 char **data_path_out, long *offset_out)
{
    /* Build ^OBJECT_NAME keyword. */
    char ptr_key[64];
    snprintf(ptr_key, sizeof(ptr_key), "^%s", object_name);

    PPvlNode *ptr_node = p_pvl_find(root, ptr_key);
    if (!ptr_node || !ptr_node->value) {
        G_warning(_("p_pds: could not find pointer keyword '%s'"), ptr_key);
        return -1;
    }

    const char *raw = ptr_node->value;
    int ok;

    /* Compute directory of label for resolving relative paths. */
    char label_dir[2048];
    strncpy(label_dir, label_path, sizeof(label_dir) - 2);
    label_dir[sizeof(label_dir)-2] = '\0';
    char *slash = strrchr(label_dir, '/');
    if (slash) *(slash + 1) = '\0';
    else { label_dir[0] = '.'; label_dir[1] = '/'; label_dir[2] = '\0'; }

    /* Case 1: bare integer → attached or detached label.
     * If unit == "BYTES": value is a direct byte offset (0-indexed).
     * If no unit (or unit == "RECORDS"): value is a 1-based record number;
     *   convert to byte offset as (N-1) * RECORD_BYTES.
     * For detached .lbl labels, the PDS3 convention is to reference the
     * data in the same file — but in practice the data lives in a companion
     * binary file (same basename, .img/.dat etc.).  Try that first. */
    if (raw[0] >= '0' && raw[0] <= '9') {
        long val = strtol(raw, NULL, 10);
        int has_bytes_unit = (ptr_node->unit &&
                              (str_ieq(ptr_node->unit,"BYTES") ||
                               str_ieq(ptr_node->unit,"BYTE")));

        /* For detached label files (.lbl), look for a companion data file. */
        if (has_label_extension(label_path)) {
            char *companion = find_companion_data_file(label_path);
            if (companion) {
                /* Companion found: use it.  BYTES offset is 0-indexed in the
                 * companion file; treat val=1 as 0 (start of file). */
                *offset_out   = has_bytes_unit ? (val > 0 ? val - 1 : 0) : 0;
                *data_path_out = companion;
                return 0;
            }
        }

        if (has_bytes_unit) {
            *offset_out = val;
        } else {
            int rec_bytes = p_pvl_value_int(root, "RECORD_BYTES", &ok);
            *offset_out = ok ? (val - 1) * (long)rec_bytes : 0;
        }
        *data_path_out = heap_str(label_path);
        return 0;
    }

    /* Case 2: sequence ("filename", offset) or ("filename", offset <BYTES>) */
    if (raw[0] == '(') {
        /* Parse filename and optional offset from the sequence string. */
        char seq[2048];
        strncpy(seq, raw + 1, sizeof(seq) - 1);
        seq[sizeof(seq)-1] = '\0';
        char *rparen = strrchr(seq, ')');
        if (rparen) *rparen = '\0';

        /* First element: filename */
        char *comma = strchr(seq, ',');
        long  byte_off = 0;
        if (comma) {
            *comma = '\0';
            char *off_str = comma + 1;
            strip_ws(off_str);
            /* Strip trailing <BYTES> unit. */
            char *unit_p = strchr(off_str, '<');
            if (unit_p) {
                /* Unit given: value is bytes directly. */
                *unit_p = '\0';
                strip_ws(off_str);
                byte_off = strtol(off_str, NULL, 10);
            }
            else {
                /* No unit: treat as record index (1-based). */
                int rec_bytes = p_pvl_value_int(root, "RECORD_BYTES", &ok);
                long rec_idx  = strtol(off_str, NULL, 10);
                byte_off = ok ? (rec_idx - 1) * (long)rec_bytes : 0;
            }
        }
        strip_ws(seq);
        strip_quotes(seq);
        strip_ws(seq);

        /* Build full path. */
        char full[2048] = {0};
        if (seq[0] == '/') {
            strncpy(full, seq, sizeof(full) - 1);
        }
        else {
            strncpy(full, label_dir, sizeof(full) - 1);
            strncat(full, seq, sizeof(full) - strlen(full) - 1);
        }

        *data_path_out = heap_str(full);
        *offset_out    = byte_off;
        return 0;
    }

    /* Case 3: bare filename string (no offset → attached or same-file). */
    {
        char fname[2048];
        strncpy(fname, raw, sizeof(fname) - 1);
        fname[sizeof(fname)-1] = '\0';
        strip_quotes(fname);
        strip_ws(fname);

        char full[2048] = {0};
        if (fname[0] == '/') {
            strncpy(full, fname, sizeof(full) - 1);
        }
        else {
            strncpy(full, label_dir, sizeof(full) - 1);
            strncat(full, fname, sizeof(full) - strlen(full) - 1);
        }

        *data_path_out = heap_str(full);
        *offset_out    = 0;
        return 0;
    }
}


/* ================================================================== */
/* scan_past_ascii: find first binary byte at or after nominal_offset  */
/*                                                                      */
/* Used to correct stale ^IMAGE pointers in attached-label PDS3 files. */
/* Strategy:                                                            */
/*   1. Read the byte at nominal_offset.                                */
/*   2. If it is already binary (not printable ASCII / common WS),     */
/*      return nominal_offset unchanged — the pointer is correct.       */
/*   3. Otherwise scan forward (up to P_PDS_LABEL_SCAN_BYTES) until    */
/*      the first binary byte, which is the true start of pixel data.  */
/* ================================================================== */

#define P_PDS_LABEL_SCAN_BYTES 512

static long scan_past_ascii(FILE *fp, long nominal_offset, long file_size)
{
    if (nominal_offset < 0) return nominal_offset;
    if (fseek(fp, nominal_offset, SEEK_SET) != 0) return nominal_offset;

    /* Read byte at nominal_offset. */
    int c = fgetc(fp);
    if (c == EOF) return nominal_offset;

    /* Already at binary pixel data — pointer is correct, no scan needed. */
    if (c > 0x7E || c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1A))
        return nominal_offset;

    /* First byte is printable/whitespace ASCII — still inside label text.
     * Scan forward for the first binary byte. */
    long scan_end = nominal_offset + P_PDS_LABEL_SCAN_BYTES;
    if (scan_end > file_size) scan_end = file_size;

    long pos = nominal_offset + 1;
    while (pos < scan_end) {
        c = fgetc(fp);
        if (c == EOF) break;
        if (c > 0x7E || c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1A))
            return pos;
        pos++;
    }
    return nominal_offset;
}

/* ================================================================== */
/* p_pds_open_image                                                     */
/* ================================================================== */

PPdsImage *p_pds_open_image(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        G_warning(_("p_pds: cannot open '%s': %s"), path, strerror(errno));
        return NULL;
    }

    /* Parse PVL label (the library reads until "END"). */
    PPvlNode *root = p_pvl_parse(path, fp);
    if (!root) {
        fclose(fp);
        return NULL;
    }

    /* Determine which object carries the image: IMAGE, QUBE, or SPECTRAL_QUBE. */
    const char *obj_names[] = { "IMAGE", "QUBE", "SPECTRAL_QUBE", NULL };
    PPvlNode   *img_obj     = NULL;
    const char *obj_name    = NULL;
    for (int i = 0; obj_names[i]; i++) {
        img_obj = pvl_find_object_deep(root, obj_names[i]);
        if (img_obj) { obj_name = obj_names[i]; break; }
    }
    if (!img_obj) {
        G_warning(_("p_pds: no IMAGE, QUBE or SPECTRAL_QUBE object in '%s'"), path);
        p_pvl_free(root);
        fclose(fp);
        return NULL;
    }

    PPdsImage *img = (PPdsImage *)G_calloc(1, sizeof(PPdsImage));
    img->label     = root;

    /* --- Read dimensions. */
    int ok;
    img->lines   = p_pvl_value_int(img_obj, "LINES",        &ok);
    if (!ok) img->lines = p_pvl_value_int(img_obj, "CORE_ITEMS_1", &ok);
    img->samples = p_pvl_value_int(img_obj, "LINE_SAMPLES", &ok);
    if (!ok) img->samples = p_pvl_value_int(img_obj, "CORE_ITEMS_2", &ok);
    if (!ok) img->samples = p_pvl_value_int(img_obj, "SAMPLES",      &ok);
    img->bands   = p_pvl_value_int(img_obj, "BANDS",        &ok);
    if (!ok) img->bands = p_pvl_value_int(img_obj, "CORE_ITEMS_3", &ok);
    if (!ok || img->bands < 1) img->bands = 1;

    /* --- Pixel type. */
    int bits = p_pvl_value_int(img_obj, "SAMPLE_BITS", &ok);
    if (!ok) bits = p_pvl_value_int(img_obj, "CORE_ITEM_BYTES", &ok);
    if (ok)  bits *= 8;  /* convert bytes → bits if CORE_ITEM_BYTES */
    else     bits  = 8;  /* fallback */
    /* Prefer SAMPLE_BITS when it looks like it already is bits. */
    {
        int sb = p_pvl_value_int(img_obj, "SAMPLE_BITS", &ok);
        if (ok) bits = sb;
    }

    const char *st = p_pvl_value(img_obj, "SAMPLE_TYPE");
    if (!st) st = p_pvl_value(img_obj, "CORE_ITEM_TYPE");
    if (!st) st = "MSB_UNSIGNED_INTEGER";

    parse_sample_type(st, bits, &img->dtype, &img->is_msb);
    img->bytes_per_pixel = bits / 8;

    /* --- Calibration. */
    img->offset         = p_pvl_value_double(img_obj, "OFFSET",         &ok);
    if (!ok) img->offset = p_pvl_value_double(img_obj, "CORE_BASE",      &ok);
    img->scaling_factor = p_pvl_value_double(img_obj, "SCALING_FACTOR",  &ok);
    if (!ok) img->scaling_factor = p_pvl_value_double(img_obj, "CORE_MULTIPLIER", &ok);
    if (!ok || img->scaling_factor == 0.0) img->scaling_factor = 1.0;

    /* --- Special pixel DN values. */
    img->dn_null = p_pvl_value_double(img_obj, "CORE_NULL",                   &ok);
    if (!ok) img->dn_null = p_pvl_value_double(img_obj, "MISSING_CONSTANT",   &ok);
    img->dn_lrs  = p_pvl_value_double(img_obj, "CORE_LOW_REPR_SATURATION",    &ok);
    img->dn_lis  = p_pvl_value_double(img_obj, "CORE_LOW_INSTR_SATURATION",   &ok);
    img->dn_hrs  = p_pvl_value_double(img_obj, "CORE_HIGH_REPR_SATURATION",   &ok);
    img->dn_his  = p_pvl_value_double(img_obj, "CORE_HIGH_INSTR_SATURATION",  &ok);

    /* --- Band organisation. */
    const char *bst = p_pvl_value(img_obj, "BAND_STORAGE_TYPE");
    if (!bst) bst = "";
    if (str_ieq(bst, "LINE_INTERLEAVED") || str_ieq(bst, "BIL"))
        img->organization = P_PDS_ORG_BIL;
    else if (str_ieq(bst, "SAMPLE_INTERLEAVED") || str_ieq(bst, "BIP"))
        img->organization = P_PDS_ORG_BIP;
    else
        img->organization = P_PDS_ORG_BSQ; /* default */

    /* --- Data file pointer. */
    char *data_path = NULL;
    long  data_off  = 0;
    if (resolve_data_pointer(root, path, obj_name, &data_path, &data_off) != 0) {
        /* Fall back: open file unbuffered to get exact post-END position. */
        data_path = heap_str(path);
        data_off  = 0; /* will be corrected by scan below */
    }

    /* Close the label file; open the data file (may be the same). */
    fclose(fp);
    fp = NULL;

    FILE *dfp = fopen(data_path, "rb");
    if (!dfp) {
        G_warning(_("p_pds: cannot open data file '%s': %s"),
                  data_path, strerror(errno));
        G_free(data_path);
        p_pds_close(img);
        return NULL;
    }

    /* Determine file size for scan bounds. */
    fseek(dfp, 0, SEEK_END);
    long file_size = ftell(dfp);

    /* For attached labels: if the byte at the computed offset is still ASCII
     * text (i.e. the ^IMAGE pointer is stale), scan forward to first binary
     * byte.  scan_past_ascii returns the offset unchanged when it is already
     * pointing at pixel data, so this is a no-op for well-formed files. */
    if (strcmp(data_path, path) == 0 && data_off >= 0) {
        long refined = scan_past_ascii(dfp, data_off, file_size);
        if (refined != data_off) {
            G_message(_("p_pds: ^IMAGE offset %ld refined to %ld (scan_past_ascii)"),
                       data_off, refined);
            data_off = refined;
        }
    }

    img->data_path   = data_path;
    img->data_offset = data_off;
    img->_fp = dfp;

    return img;
}

/* ================================================================== */
/* Row reading                                                          */
/* ================================================================== */

/*
 * Map a raw on-disk DN value (already byte-swapped if needed) to double.
 * Returns the physical value: physical = offset + DN * scaling_factor.
 */
static double dn_to_double(PPdsImage *img, const void *raw_ptr)
{
    double dn;
    switch (img->dtype) {
    case P_PDS_DTYPE_UINT8:
        dn = (double)(*(const uint8_t *)raw_ptr);
        break;
    case P_PDS_DTYPE_INT16:
        dn = (double)(*(const int16_t *)raw_ptr);
        break;
    case P_PDS_DTYPE_UINT16:
        dn = (double)(*(const uint16_t *)raw_ptr);
        break;
    case P_PDS_DTYPE_INT32:
        dn = (double)(*(const int32_t *)raw_ptr);
        break;
    case P_PDS_DTYPE_UINT32:
        dn = (double)(*(const uint32_t *)raw_ptr);
        break;
    case P_PDS_DTYPE_FLOAT32: {
        float f;
        memcpy(&f, raw_ptr, 4);
        dn = (double)f;
        break;
    }
    case P_PDS_DTYPE_FLOAT64:
        memcpy(&dn, raw_ptr, 8);
        break;
    default:
        dn = 0.0;
    }
    return img->offset + dn * img->scaling_factor;
}

/* Check if raw DN matches a special-pixel value (before calibration). */
static int is_special_dn(PPdsImage *img, double raw_dn,
                          double *grass_val_out)
{
    /* Tolerate floating-point rounding with epsilon. */
    double eps = 0.5;
    if (fabs(raw_dn - img->dn_null) < eps) {
        /* GRASS NULL — signal via NaN sentinel; caller sets Rast_set_null(). */
        *grass_val_out = NAN;
        return 1;
    }
    /* Other special pixels are left as-is (their DN values map to extreme
     * physical values after calibration).  Modules can test with
     * Rast_is_d_null_value() and set their own masks. */
    return 0;
}

int p_pds_read_row(PPdsImage *img, int band, int row,
                    double *buf, int grass_special)
{
    if (!img || !img->_fp || !buf) return -1;
    if (row  < 0 || row  >= img->lines)  return -1;
    if (band < 0 || band >= img->bands)  return -1;

    FILE *fp  = (FILE *)img->_fp;
    int   bpp = img->bytes_per_pixel;
    int   ns  = img->samples;

    /* Compute file offset of the first byte of this row. */
    long row_bytes = (long)ns * bpp;
    long band_size = row_bytes * (long)img->lines;
    long seek_pos;

    switch (img->organization) {
    case P_PDS_ORG_BSQ:
        seek_pos = img->data_offset
                   + (long)band * band_size
                   + (long)row  * row_bytes;
        break;
    case P_PDS_ORG_BIL:
        seek_pos = img->data_offset
                   + (long)row  * (row_bytes * (long)img->bands)
                   + (long)band * row_bytes;
        break;
    case P_PDS_ORG_BIP:
        seek_pos = img->data_offset
                   + (long)row * (long)ns * (long)img->bands * bpp
                   + (long)band * bpp;
        break;
    default:
        return -1;
    }

    /* Allocate a raw byte buffer for one row (or one element for BIP). */
    int need_swap = (img->is_msb == p_pds_is_little_endian()) && bpp > 1;

    if (img->organization != P_PDS_ORG_BIP) {
        /* BSQ / BIL: read entire row contiguously. */
        uint8_t *raw = (uint8_t *)G_malloc((size_t)ns * bpp);

        if (fseek(fp, seek_pos, SEEK_SET) != 0 ||
            fread(raw, bpp, ns, fp) != (size_t)ns) {
            G_warning(_("p_pds: read error at row %d band %d"), row, band);
            G_free(raw);
            return -1;
        }

        if (need_swap)
            p_pds_swap_bytes(raw, ns, bpp);

        for (int s = 0; s < ns; s++) {
            double phys = dn_to_double(img, raw + (size_t)s * bpp);
            if (grass_special) {
                double gv;
                if (is_special_dn(img, phys, &gv)) {
                    /* Caller expects Rast_set_d_null_value() — use NaN as
                     * sentinel; p.pds3.in will call Rast_set_d_null_value(). */
                    buf[s] = NAN;
                    continue;
                }
            }
            buf[s] = phys;
        }
        G_free(raw);
    }
    else {
        /* BIP: samples are interleaved; skip (bands * bpp) between samples. */
        uint8_t *elem = (uint8_t *)G_malloc((size_t)bpp);
        long stride = (long)img->bands * bpp;

        for (int s = 0; s < ns; s++) {
            long pos = seek_pos + (long)s * stride;
            if (fseek(fp, pos, SEEK_SET) != 0 ||
                fread(elem, bpp, 1, fp) != 1) {
                G_warning(_("p_pds: read error at row %d, sample %d, band %d"),
                           row, s, band);
                G_free(elem);
                return -1;
            }
            if (need_swap)
                p_pds_swap_bytes(elem, 1, bpp);

            double phys = dn_to_double(img, elem);
            if (grass_special) {
                double gv;
                if (is_special_dn(img, phys, &gv)) {
                    buf[s] = NAN;
                    continue;
                }
            }
            buf[s] = phys;
        }
        G_free(elem);
    }

    return 0;
}

int p_pds_read_band(PPdsImage *img, int band, double *buf, int grass_special)
{
    /*
     * OpenMP parallelisation over rows.  p_pds_read_row() is safe to call
     * from multiple threads simultaneously because each row's fseek+fread
     * operates on a different file region.  However, FILE* seeks are NOT
     * thread-safe on POSIX.  We therefore use pread(2) semantics by opening
     * a private FILE* per thread when OpenMP is active.
     *
     * For simplicity and portability we parallelise by chunking rows across
     * threads, each thread managing its own file descriptor via a private
     * FILE* opened from img->data_path.
     */
    int nrows = img->lines;
    int ns    = img->samples;
    int err   = 0;

#ifdef _OPENMP
#pragma omp parallel shared(err)
    {
        FILE *tfp = fopen(img->data_path, "rb");
        if (!tfp) {
#pragma omp atomic write
            err = 1;
        }
        else {
            /* Use a temporary PPdsImage copy that points to the thread-local fp. */
            PPdsImage local = *img;
            local._fp = tfp;

#pragma omp for schedule(static)
            for (int row = 0; row < nrows; row++) {
                if (p_pds_read_row(&local, band, row,
                                    buf + (size_t)row * ns,
                                    grass_special) != 0) {
#pragma omp atomic write
                    err = 1;
                }
            }
            fclose(tfp);
        }
    } /* end parallel */
#else
    for (int row = 0; row < nrows; row++) {
        if (p_pds_read_row(img, band, row,
                            buf + (size_t)row * ns,
                            grass_special) != 0) {
            err = 1;
            break;
        }
    }
#endif

    return err ? -1 : 0;
}

/* ================================================================== */
/* Close / cleanup                                                      */
/* ================================================================== */

void p_pds_close(PPdsImage *img)
{
    if (!img) return;
    if (img->_fp) {
        fclose((FILE *)img->_fp);
        img->_fp = NULL;
    }
    G_free(img->data_path);
    p_pvl_free(img->label);
    G_free(img);
}
