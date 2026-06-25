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

/* Merge in keywords from a referenced external "structure" file
 * (^STRUCTURE = "filename.fmt"), a real PDS3 convention some QUBE
 * archives use to factor CORE_ and SUFFIX_ descriptor keywords out of
 * the main label and into one or more small shared format files (e.g.
 * Cassini VIMS's core_description.fmt/suffix_description.fmt, each
 * referenced via its own "^STRUCTURE = ..." line inside
 * OBJECT = SPECTRAL_QUBE, rather than being inlined). Splices each
 * referenced file's top-level keywords/groups directly as additional
 * children of the object that points to them, so a plain
 * p_pvl_value(img_obj, "...") lookup finds them exactly as if they had
 * been inlined. Missing/unreadable structure files only warn (some
 * keywords end up missing, handled by the existing per-keyword
 * fallbacks/defaults already in p_pds_open_image_named()), they don't
 * fail the whole import. */
static void resolve_structure_pointers(PPvlNode *node, const char *label_path)
{
    if (!node) return;

    for (PPvlNode *child = node->children; child; child = child->next) {
        if (child->type != P_PVL_SCALAR || !str_ieq(child->key, "^STRUCTURE") ||
            !child->value || !child->value[0])
            continue;

        char label_dir[2048];
        strncpy(label_dir, label_path, sizeof(label_dir) - 2);
        label_dir[sizeof(label_dir)-2] = '\0';
        char *slash = strrchr(label_dir, '/');
        if (slash) *(slash + 1) = '\0';
        else { label_dir[0] = '.'; label_dir[1] = '/'; label_dir[2] = '\0'; }

        char fmt_path[2048];
        if (child->value[0] == '/')
            snprintf(fmt_path, sizeof(fmt_path), "%s", child->value);
        else
            snprintf(fmt_path, sizeof(fmt_path), "%s%s", label_dir, child->value);

        FILE *ffp = fopen(fmt_path, "r");
        if (!ffp) {
            G_warning(_("p_pds: ^STRUCTURE referenced '%s' not found "
                        "(looked for '%s') -- some keywords may be "
                        "missing."), child->value, fmt_path);
            continue;
        }
        PPvlNode *extra = pvl_parse_block(ffp, fmt_path, NULL, NULL);
        fclose(ffp);
        if (!extra) continue;

        PPvlNode *tail = node->children;
        while (tail->next) tail = tail->next;
        tail->next = extra;
    }

    /* Recurse into nested OBJECT/GROUP children (over the original
     * structure; the new content spliced in above is flat scalars/
     * groups, not further nested image objects, so no need to revisit
     * it here). */
    for (PPvlNode *child = node->children; child; child = child->next) {
        if (child->type == P_PVL_OBJECT || child->type == P_PVL_GROUP)
            resolve_structure_pointers(child, label_path);
    }
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

/* True if s ends with suffix (case-insensitive). */
static int str_iendswith(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return 0;
    return str_ieq(s + (ls - lf), suffix);
}

/* Recursive depth-first search for a scalar key anywhere under parent
 * (forward declaration; defined below, used here and by
 * resolve_data_pointer()/record_bytes_deep()). */
static PPvlNode *pvl_find_scalar_deep(const PPvlNode *parent, const char *key);

/* Fallback for archives that name their image object something other than
 * the three well-known PDS3 names (e.g. JPL PDS Imaging Node M3 L1B
 * products use "OBJECT = RDN_IMAGE" nested inside "OBJECT = RDN_FILE",
 * with pointer keyword "^RDN_IMAGE" -- not "IMAGE"/"QUBE"/"SPECTRAL_QUBE").
 * Depth-first search for any OBJECT/GROUP whose name ends in "_IMAGE" or
 * "_QUBE" that also has a matching "^<name>" pointer somewhere in the
 * label (the pointer requirement avoids matching unrelated description
 * objects that merely happen to end in "_IMAGE"). *root* is the true
 * label root (constant across the recursion, needed for the pointer
 * search regardless of how deep the candidate object is nested);
 * *parent* is the node currently being scanned. Returns the object node
 * and sets *out_name to its name (owned by the PVL tree) or NULL. */
static PPvlNode *pvl_find_image_object_by_suffix(const PPvlNode *root,
                                                   const PPvlNode *parent,
                                                   const char **out_name)
{
    if (!parent) return NULL;
    PPvlNode *n = parent->children;
    while (n) {
        if ((n->type == P_PVL_OBJECT || n->type == P_PVL_GROUP) && n->value &&
            (str_iendswith(n->value, "_IMAGE") || str_iendswith(n->value, "_QUBE"))) {
            char ptr_key[80];
            snprintf(ptr_key, sizeof(ptr_key), "^%s", n->value);
            if (pvl_find_scalar_deep(root, ptr_key)) {
                *out_name = n->value;
                return n;
            }
        }
        n = n->next;
    }
    /* Recurse into each child OBJECT/GROUP. */
    n = parent->children;
    while (n) {
        if (n->type == P_PVL_OBJECT || n->type == P_PVL_GROUP) {
            PPvlNode *hit = pvl_find_image_object_by_suffix(root, n, out_name);
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

/* Recursive depth-first search for a scalar key (e.g. a ^POINTER keyword)
 * anywhere under parent, including inside nested OBJECT/GROUP blocks.
 * Needed because some PDS3 archives (e.g. MRO/CRISM TRDR) wrap multiple
 * data objects in an enclosing "OBJECT = FILE" block, placing pointer
 * keywords like ^IMAGE one level below the label root rather than at the
 * top level that p_pvl_find() alone would see. */
static PPvlNode *pvl_find_scalar_deep(const PPvlNode *parent, const char *key)
{
    if (!parent) return NULL;
    PPvlNode *hit = p_pvl_find(parent, key);
    if (hit) return hit;
    PPvlNode *n = parent->children;
    while (n) {
        if (n->type == P_PVL_OBJECT || n->type == P_PVL_GROUP) {
            hit = pvl_find_scalar_deep(n, key);
            if (hit) return hit;
        }
        n = n->next;
    }
    return NULL;
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

/* Parse the *index*'th (0-based) integer out of a PVL tuple value such as
 * "(64,352,672)" or "( 64, 352, 672 )". Returns 1 on success, 0 if the
 * value isn't a tuple or doesn't have that many elements. */
static int pvl_tuple_int(const char *v, int index, int *out)
{
    if (!v) return 0;
    const char *p = strchr(v, '(');
    if (!p) return 0;
    p++;
    for (int i = 0; i < index; i++) {
        p = strchr(p, ',');
        if (!p) return 0;
        p++;
    }
    char *end;
    long val = strtol(p, &end, 0);
    if (end == p) return 0;
    *out = (int)val;
    return 1;
}

/* Parse the *index*'th (0-based) bare/quoted token out of a PVL tuple
 * value such as "(SAMPLE,BAND,LINE)". Writes into out (caller buffer,
 * length outlen), upper-cased, trimmed of quotes/whitespace. Returns 1 on
 * success, 0 if the value isn't a tuple or doesn't have that many
 * elements. */
static int pvl_tuple_token(const char *v, int index, char *out, size_t outlen)
{
    if (!v) return 0;
    const char *p = strchr(v, '(');
    if (!p) return 0;
    p++;
    for (int i = 0; i < index; i++) {
        p = strchr(p, ',');
        if (!p) return 0;
        p++;
    }
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = 0;
    while (*p && *p != ',' && *p != ')' && n < outlen - 1) {
        if (!isspace((unsigned char)*p) && *p != '"')
            out[n++] = (char)toupper((unsigned char)*p);
        p++;
    }
    out[n] = '\0';
    return n > 0;
}

/* Read a 3-tuple keyword (e.g. CORE_ITEMS or SUFFIX_ITEMS), ordered per
 * AXIS_NAME -- the real PDS3 QUBE object convention, e.g. OMEGA/VIMS
 * *_QUBE objects: "AXIS_NAME = (SAMPLE,BAND,LINE)", "CORE_ITEMS =
 * (64,352,672)" means 64 samples, 352 bands, 672 lines -- NOT the fixed
 * sample/band/line order some other PDS3 IMAGE objects use. Falls back
 * to the common (SAMPLE,BAND,LINE) order if AXIS_NAME is absent.
 * Returns 1 if the tuple keyword was present and fully parsed (zero
 * values are valid, e.g. SUFFIX_ITEMS often has a zero line-suffix). */
static int pvl_tuple_by_axis(const PPvlNode *img_obj, const char *keyword,
                              int *samples, int *bands, int *lines)
{
    const char *tuple_v = p_pvl_value(img_obj, keyword);
    if (!tuple_v) return 0;
    const char *axis_v = p_pvl_value(img_obj, "AXIS_NAME");

    int items[3];
    if (!pvl_tuple_int(tuple_v, 0, &items[0]) ||
        !pvl_tuple_int(tuple_v, 1, &items[1]) ||
        !pvl_tuple_int(tuple_v, 2, &items[2]))
        return 0;

    const char *names[3] = { "SAMPLE", "BAND", "LINE" }; /* default order */
    char tok[16];
    if (axis_v) {
        for (int i = 0; i < 3; i++) {
            if (pvl_tuple_token(axis_v, i, tok, sizeof(tok))) {
                if (strncmp(tok, "SAMPLE", 6) == 0) names[i] = "SAMPLE";
                else if (strncmp(tok, "BAND", 4) == 0) names[i] = "BAND";
                else if (strncmp(tok, "LINE", 4) == 0) names[i] = "LINE";
            }
        }
    }

    *samples = *bands = *lines = 0;
    for (int i = 0; i < 3; i++) {
        if (strcmp(names[i], "SAMPLE") == 0) *samples = items[i];
        else if (strcmp(names[i], "BAND") == 0) *bands = items[i];
        else if (strcmp(names[i], "LINE") == 0) *lines = items[i];
    }
    return 1;
}

static int pvl_core_items_by_axis(const PPvlNode *img_obj,
                                    int *samples, int *bands, int *lines)
{
    if (!pvl_tuple_by_axis(img_obj, "CORE_ITEMS", samples, bands, lines))
        return 0;
    return (*samples > 0 && *lines > 0);
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
/* RECORD_BYTES may live at label root or, in FILE-wrapped labels (e.g.
 * MRO/CRISM TRDR), one level down inside the enclosing OBJECT = FILE
 * block — use the recursive scalar search so both layouts resolve. */
static int record_bytes_deep(const PPvlNode *root, int *ok)
{
    PPvlNode *n = pvl_find_scalar_deep(root, "RECORD_BYTES");
    if (!n || !n->value) { *ok = 0; return 0; }
    char *end;
    long v = strtol(n->value, &end, 0);
    if (end == n->value) { *ok = 0; return 0; }
    *ok = 1;
    return (int)v;
}

static int resolve_data_pointer(PPvlNode *root, const char *label_path,
                                 const char *object_name,
                                 char **data_path_out, long *offset_out)
{
    /* Build ^OBJECT_NAME keyword. */
    char ptr_key[64];
    snprintf(ptr_key, sizeof(ptr_key), "^%s", object_name);

    PPvlNode *ptr_node = pvl_find_scalar_deep(root, ptr_key);
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
            int rec_bytes = record_bytes_deep(root, &ok);
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
                int rec_bytes = record_bytes_deep(root, &ok);
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
/*   1. Read a short run of bytes at nominal_offset (P_PDS_ASCII_RUN).  */
/*   2. If even one of them is binary (not printable ASCII / common    */
/*      WS), the pointer already lands on real pixel data -- return    */
/*      nominal_offset unchanged. A single coincidentally ASCII-range   */
/*      byte (e.g. a 16-bit pixel whose high byte is a small positive   */
/*      value like 0x09) is expected by chance in real binary data and  */
/*      must not trigger a shift; real PVL label text, by contrast,     */
/*      always runs many consecutive printable bytes.                   */
/*   3. Otherwise scan forward (up to P_PDS_LABEL_SCAN_BYTES) until    */
/*      a run of P_PDS_ASCII_RUN consecutive binary bytes is found,    */
/*      which is the true start of pixel data.                          */
/* ================================================================== */

#define P_PDS_LABEL_SCAN_BYTES 512
#define P_PDS_ASCII_RUN 4

static int is_ascii_textlike(int c)
{
    return !(c > 0x7E || c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1A));
}

/* True if all P_PDS_ASCII_RUN bytes starting at fp's current position
 * (which is restored on return) look like printable/whitespace ASCII. */
static int run_is_ascii(FILE *fp, long pos, long file_size)
{
    if (pos + P_PDS_ASCII_RUN > file_size) return 0;
    long save = ftell(fp);
    fseek(fp, pos, SEEK_SET);
    int all_ascii = 1;
    for (int i = 0; i < P_PDS_ASCII_RUN; i++) {
        int c = fgetc(fp);
        if (c == EOF || !is_ascii_textlike(c)) { all_ascii = 0; break; }
    }
    fseek(fp, save, SEEK_SET);
    return all_ascii;
}

static long scan_past_ascii(FILE *fp, long nominal_offset, long file_size)
{
    if (nominal_offset < 0) return nominal_offset;

    /* Already at (or within a run of) binary pixel data — pointer is
     * correct, no scan needed. */
    if (!run_is_ascii(fp, nominal_offset, file_size))
        return nominal_offset;

    /* A real run of printable/whitespace ASCII — still inside label
     * text. Scan forward for the first position starting a binary run. */
    long scan_end = nominal_offset + P_PDS_LABEL_SCAN_BYTES;
    if (scan_end > file_size) scan_end = file_size;

    for (long pos = nominal_offset + 1; pos < scan_end; pos++) {
        if (!run_is_ascii(fp, pos, file_size))
            return pos;
    }
    return nominal_offset;
}

/* ================================================================== */
/* p_pds_open_image                                                     */
/* ================================================================== */

PPdsImage *p_pds_open_image(const char *path)
{
    return p_pds_open_image_named(path, NULL);
}

PPdsImage *p_pds_open_image_named(const char *path, const char *object_name)
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
    resolve_structure_pointers(root, path);

    PPvlNode   *img_obj  = NULL;
    const char *obj_name = NULL;

    if (object_name) {
        /* Caller asked for a specific OBJECT by name (e.g. labels with
         * several image objects side by side, such as M3 L1B's
         * RDN_IMAGE/LOC_IMAGE/OBS_IMAGE) -- look for exactly that one. */
        img_obj = pvl_find_object_deep(root, object_name);
        if (img_obj) obj_name = object_name;
    } else {
        /* Determine which object carries the image: IMAGE, QUBE, or SPECTRAL_QUBE. */
        const char *obj_names[] = { "IMAGE", "QUBE", "SPECTRAL_QUBE", NULL };
        for (int i = 0; obj_names[i]; i++) {
            img_obj = pvl_find_object_deep(root, obj_names[i]);
            if (img_obj) { obj_name = obj_names[i]; break; }
        }
        if (!img_obj) {
            /* None of the three standard names matched -- some archives use
             * their own custom object name (e.g. JPL PDS Imaging Node M3 L1B
             * products: "OBJECT = RDN_IMAGE", pointer "^RDN_IMAGE"). Fall back
             * to any *_IMAGE/*_QUBE object that has a matching pointer. */
            img_obj = pvl_find_image_object_by_suffix(root, root, &obj_name);
        }
    }
    if (!img_obj) {
        if (object_name)
            G_warning(_("p_pds: no OBJECT named '%s' in '%s'"),
                      object_name, path);
        else
            G_warning(_("p_pds: no IMAGE, QUBE, SPECTRAL_QUBE, or *_IMAGE/*_QUBE "
                        "object in '%s'"), path);
        p_pvl_free(root);
        fclose(fp);
        return NULL;
    }

    PPdsImage *img = (PPdsImage *)G_calloc(1, sizeof(PPdsImage));
    img->label     = root;

    /* --- Read dimensions. */
    int ok;
    img->lines   = p_pvl_value_int(img_obj, "LINES",        &ok);
    img->samples = p_pvl_value_int(img_obj, "LINE_SAMPLES", &ok);
    img->bands   = p_pvl_value_int(img_obj, "BANDS",        &ok);
    if (!img->lines) img->lines = p_pvl_value_int(img_obj, "CORE_ITEMS_1", &ok);
    if (!img->samples) {
        img->samples = p_pvl_value_int(img_obj, "CORE_ITEMS_2", &ok);
        if (!ok) img->samples = p_pvl_value_int(img_obj, "SAMPLES", &ok);
    }
    if (!img->bands) img->bands = p_pvl_value_int(img_obj, "CORE_ITEMS_3", &ok);
    if (!img->lines || !img->samples) {
        /* CORE_ITEMS tuple convention, ordered per AXIS_NAME (real PDS3
         * QUBE products, e.g. OMEGA/VIMS *_QUBE objects -- CORE_ITEMS is
         * one tuple-valued keyword, not three separate _1/_2/_3 ones). */
        int s, b, l;
        if (pvl_core_items_by_axis(img_obj, &s, &b, &l)) {
            if (!img->samples) img->samples = s;
            if (!img->bands)   img->bands   = b;
            if (!img->lines)   img->lines   = l;
        }
    }
    if (img->bands < 1) img->bands = 1;

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
    /* When neither keyword is present (e.g. MRO/CRISM TRDR labels), do NOT
     * default to 0.0: is_special_dn()'s epsilon=0.5 window would then null
     * every real sample within [-0.5, 0.5] — silently destroying almost all
     * legitimate reflectance/I-F data, which lives in exactly that range.
     * NaN never matches the epsilon comparison, so no value is treated as
     * special when the product defines no null constant. */
    if (!ok) img->dn_null = NAN;
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
    else if (bst[0] == '\0' && p_pvl_value(img_obj, "AXIS_NAME")) {
        /* Real PDS3 QUBE objects (AXES/AXIS_NAME present) often omit
         * BAND_STORAGE_TYPE entirely -- the AXIS_NAME order itself *is*
         * the storage order (fastest-varying axis listed first): e.g.
         * Cassini VIMS's "AXIS_NAME = (SAMPLE,BAND,LINE)" with no
         * BAND_STORAGE_TYPE keyword at all is still BIL, confirmed
         * against NASA's own ISIS3 vims2isis importer (ReadVimsBIL(),
         * which hardcodes BIL for exactly this axis order without ever
         * consulting BAND_STORAGE_TYPE). */
        char ax0[16] = "", ax1[16] = "";
        const char *axis_v = p_pvl_value(img_obj, "AXIS_NAME");
        pvl_tuple_token(axis_v, 0, ax0, sizeof(ax0));
        pvl_tuple_token(axis_v, 1, ax1, sizeof(ax1));
        if (str_ieq(ax0, "SAMPLE") && str_ieq(ax1, "BAND"))
            img->organization = P_PDS_ORG_BIL;
        else if (str_ieq(ax0, "BAND") && str_ieq(ax1, "SAMPLE"))
            img->organization = P_PDS_ORG_BIP;
        else
            img->organization = P_PDS_ORG_BSQ; /* (SAMPLE,LINE,BAND) or similar */
    }
    else
        img->organization = P_PDS_ORG_BSQ; /* default */

    /* --- Line prefix (e.g. Cassini ISS dark/overclocked pixels). */
    img->line_prefix_bytes = p_pvl_value_int(img_obj, "LINE_PREFIX_BYTES", &ok);
    if (!ok) img->line_prefix_bytes = 0;

    /* --- Refuse QUBE sideplanes (SUFFIX_ITEMS) rather than silently
     * misreading. Real PDS3 QUBE products (e.g. ESA Mars Express OMEGA,
     * Cassini VIMS raw .qub) append extra sample-/band-direction
     * "sideplane" bytes per record. Two supported cases:
     * 1. BAND_STORAGE_TYPE = LINE_INTERLEAVED (BIL) with a zero
     *    line-suffix (matching NASA's own ISIS3 ReadVimsBIL() importer):
     *    a sample-suffix block of (sample-suffix-items * item-bytes)
     *    bytes is appended after each band's core samples within a line,
     *    and a band-suffix backplane of (band-suffix-items rows, each
     *    (samples + sample-suffix-items) items wide) is appended once
     *    per line after all bands.
     * 2. BAND_STORAGE_TYPE = BAND_SEQUENTIAL_BY_PIXEL (BIP), zero
     *    band-suffix and zero line-suffix (matching real ESA Venus
     *    Express/Rosetta VIRTIS QUBEs, and ISIS3's own generic
     *    ProcessImport::ProcessBip() suffix handling): a sample-suffix
     *    block of (sample-suffix-items * item-bytes) bytes is appended
     *    after each real sample's per-band spectrum, i.e. each line
     *    becomes (real samples + sample-suffix-items) "samples" wide,
     *    every one of them a full per-band spectrum. Verified against a
     *    real downloaded VIRTIS-H QUBE: decoding with this stride
     *    produces smooth, physically coherent sample-to-sample and
     *    line-to-line spectral continuity (see TODO.md).
     * Any other organisation, or a nonzero band-suffix/line-suffix, has
     * no verified byte layout here and is refused rather than guessed --
     * reading on as if SUFFIX_ITEMS were (0,0,0) would silently shift
     * every subsequent record, producing wrong-but-plausible-looking
     * pixel values instead of an obvious failure. */
    img->suffix_sample_items = img->suffix_band_items = img->suffix_line_items = 0;
    img->suffix_item_bytes = 4; /* matches every real archive seen so far */
    {
        int sfx_s, sfx_b, sfx_l;
        if (pvl_tuple_by_axis(img_obj, "SUFFIX_ITEMS", &sfx_s, &sfx_b, &sfx_l) &&
            (sfx_s != 0 || sfx_b != 0 || sfx_l != 0)) {
            int bip_ok = (img->organization == P_PDS_ORG_BIP && sfx_b == 0);
            int bil_ok = (img->organization == P_PDS_ORG_BIL);
            if (sfx_l != 0 || !(bip_ok || bil_ok)) {
                G_warning(_("p_pds: '%s' has SUFFIX_ITEMS (sample=%d band=%d "
                            "line=%d) -- this reader only supports skipping "
                            "sample/band suffix bytes for BIL cubes, or "
                            "sample-suffix-only for BIP cubes, both with a "
                            "zero line-suffix. Refusing to silently misread "
                            "the cube. Not supported yet."),
                          path, sfx_s, sfx_b, sfx_l);
                p_pvl_free(root);
                fclose(fp);
                G_free(img);
                return NULL;
            }
            int ok2;
            int item_bytes = p_pvl_value_int(img_obj, "SUFFIX_ITEM_BYTES", &ok2);
            if (!ok2)
                item_bytes = p_pvl_value_int(img_obj, "SAMPLE_SUFFIX_ITEM_BYTES", &ok2);
            img->suffix_sample_items = sfx_s;
            img->suffix_band_items   = sfx_b;
            if (ok2 && item_bytes > 0) img->suffix_item_bytes = item_bytes;
            G_message(_("p_pds: '%s' has QUBE suffix bytes (sample=%d, "
                        "band=%d, %d bytes/item) -- skipped on read, not "
                        "exposed as extra bands."),
                      path, sfx_s, sfx_b, img->suffix_item_bytes);
        }
    }

    /* Cross-check (and, when it disagrees, override) the assumed BIL
     * per-line stride against the label's own fixed-record byte
     * accounting -- see the line_stride_bytes comment in p_pds.h. Only
     * meaningful when there's a real suffix backplane to get wrong. */
    img->line_stride_bytes = 0;
    if (img->organization == P_PDS_ORG_BIL &&
        (img->suffix_sample_items || img->suffix_band_items)) {
        const char *rt = p_pvl_value(root, "RECORD_TYPE");
        int ok_fr, ok_rb, ok_lr;
        int file_records  = p_pvl_value_int(root, "FILE_RECORDS", &ok_fr);
        int record_bytes  = p_pvl_value_int(root, "RECORD_BYTES", &ok_rb);
        int label_records = p_pvl_value_int(root, "LABEL_RECORDS", &ok_lr);
        if (rt && str_ieq(rt, "FIXED_LENGTH") && ok_fr && ok_rb && ok_lr &&
            img->lines > 0) {
            long data_bytes = (long)(file_records - label_records) * record_bytes;
            if (data_bytes > 0 && data_bytes % img->lines == 0) {
                long assumed_stride =
                    ((long)img->samples * img->bytes_per_pixel +
                     (long)img->line_prefix_bytes +
                     (long)img->suffix_sample_items * img->suffix_item_bytes) *
                        (long)img->bands +
                    (long)img->suffix_band_items *
                        ((long)img->samples + img->suffix_sample_items) *
                        img->suffix_item_bytes;
                long real_stride = data_bytes / img->lines;
                if (real_stride != assumed_stride) {
                    G_message(_("p_pds: '%s' real per-line byte stride "
                                "(%ld, from FILE_RECORDS/RECORD_BYTES/"
                                "LABEL_RECORDS) differs from the assumed "
                                "(samples + sample-suffix-items) band-suffix "
                                "width (%ld) -- using the real, label-"
                                "derived stride (e.g. MEX OMEGA's band-"
                                "suffix rows are exactly `samples` items "
                                "wide, unlike Cassini VIMS's)."),
                              path, real_stride, assumed_stride);
                }
                /* Always prefer the label-derived ground truth when
                 * available, even when it agrees with the assumption --
                 * one fewer thing for read_band_suffix_row() to assume. */
                img->line_stride_bytes = real_stride;
            }
        }
    }

    /* --- Data file pointer. */
    char *data_path = NULL;
    long  data_off  = 0;
    int   ptr_found = (resolve_data_pointer(root, path, obj_name,
                                             &data_path, &data_off) == 0);
    if (!ptr_found) {
        /* The pointer keyword doesn't always match the OBJECT's own type
         * name (e.g. real Cassini VIMS labels: "OBJECT = SPECTRAL_QUBE"
         * but "^QUBE = (...)", not "^SPECTRAL_QUBE") -- retry the other
         * standard pointer names before giving up. */
        static const char *alt_names[] = { "IMAGE", "QUBE", "SPECTRAL_QUBE" };
        for (size_t i = 0; i < 3 && !ptr_found; i++) {
            if (str_ieq(alt_names[i], obj_name ? obj_name : ""))
                continue;
            ptr_found = (resolve_data_pointer(root, path, alt_names[i],
                                               &data_path, &data_off) == 0);
        }
    }
    if (!ptr_found) {
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

    /* Compute file offset of the first byte of this row.
     * record_stride accounts for LINE_PREFIX_BYTES prepended to each row on
     * disk (e.g. Cassini ISS stores 24 bytes of dark/OC pixels before each
     * line of image data).  seek_pos lands on the first *image* byte. */
    long row_bytes    = (long)ns * bpp;
    long pfx          = (long)img->line_prefix_bytes;
    long record_stride = row_bytes + pfx;
    long band_size    = record_stride * (long)img->lines;
    long seek_pos;

    switch (img->organization) {
    case P_PDS_ORG_BSQ:
        seek_pos = img->data_offset
                   + (long)band * band_size
                   + (long)row  * record_stride
                   + pfx;
        break;
    case P_PDS_ORG_BIL: {
        /* QUBE sample-/band-suffix bytes (e.g. Cassini VIMS): a
         * sample-suffix block is appended after each band's core
         * samples within a line, and a band-suffix backplane is
         * appended once per line after all bands -- both zero for
         * cubes without suffix items, reducing to the plain BIL stride
         * below. See p_pds_open_image_named()'s SUFFIX_ITEMS handling. */
        long samp_sfx_bytes = (long)img->suffix_sample_items * img->suffix_item_bytes;
        long band_record_stride = record_stride + samp_sfx_bytes;
        long line_backplane_bytes = (long)img->suffix_band_items *
                                     ((long)ns + img->suffix_sample_items) *
                                     img->suffix_item_bytes;
        long full_line_stride = img->line_stride_bytes > 0
                                     ? img->line_stride_bytes
                                     : band_record_stride * (long)img->bands +
                                           line_backplane_bytes;
        seek_pos = img->data_offset
                   + (long)row  * full_line_stride
                   + (long)band * band_record_stride
                   + pfx;
        break;
    }
    case P_PDS_ORG_BIP: {
        /* QUBE sample-suffix bytes (e.g. Venus Express/Rosetta VIRTIS): a
         * sample-suffix block is appended after each real sample's
         * per-band spectrum, widening every line by suffix_sample_items
         * "samples" -- zero for cubes without suffix items, reducing to
         * the plain BIP stride below. See the SUFFIX_ITEMS handling
         * above for the verification this layout is based on. */
        long samp_sfx_bytes = (long)img->suffix_sample_items * img->suffix_item_bytes;
        long bip_sample_stride = (long)img->bands * bpp + samp_sfx_bytes;
        seek_pos = img->data_offset
                   + (long)row * (long)ns * bip_sample_stride
                   + (long)band * bpp;
        break;
    }
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
        /* BIP: samples are interleaved; skip (bands * bpp), plus any
         * sample-suffix bytes, between samples. */
        uint8_t *elem = (uint8_t *)G_malloc((size_t)bpp);
        long stride = (long)img->bands * bpp +
                      (long)img->suffix_sample_items * img->suffix_item_bytes;

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

int p_pds_read_band_suffix_row(PPdsImage *img, int suffix_index, int row,
                                double *buf)
{
    if (!img || !img->_fp || !buf) return -1;
    if (row < 0 || row >= img->lines) return -1;
    if (suffix_index < 0 || suffix_index >= img->suffix_band_items) return -1;
    if (img->organization != P_PDS_ORG_BIL) return -1;

    FILE *fp  = (FILE *)img->_fp;
    int   bpp = img->bytes_per_pixel;
    int   ns  = img->samples;
    int   sfx_bpp = img->suffix_item_bytes;
    long  pfx = (long)img->line_prefix_bytes;

    long row_bytes      = (long)ns * bpp;
    long record_stride  = row_bytes + pfx;
    long samp_sfx_bytes = (long)img->suffix_sample_items * sfx_bpp;
    long band_record_stride = record_stride + samp_sfx_bytes;
    long assumed_suffix_row_bytes =
        ((long)ns + img->suffix_sample_items) * sfx_bpp;
    long assumed_full_line_stride = band_record_stride * (long)img->bands +
        (long)img->suffix_band_items * assumed_suffix_row_bytes;

    /* Band-suffix backplane rows are appended once per line, after all
     * real bands (each padded to band_record_stride, including their own
     * sample-suffix slot). Real archives disagree on each row's own
     * width, though: Cassini VIMS pads to (samples + suffix_sample_items)
     * items (real ISIS3 ReadVimsBIL() source), but MEX OMEGA's real
     * archived QUBE uses exactly `samples` items per row -- confirmed via
     * exact byte-count arithmetic, see line_stride_bytes in p_pds.h. When
     * the label gave us that ground truth, derive the real per-row width
     * from it instead of assuming; otherwise fall back to the
     * (samples + suffix_sample_items) assumption. suffix_index selects
     * which of the suffix_band_items rows (e.g. OMEGA's 7 housekeeping
     * side-planes, index 0 = scanning mirror position -- OMEGA_HK.TXT). */
    long full_line_stride = img->line_stride_bytes > 0
                                 ? img->line_stride_bytes
                                 : assumed_full_line_stride;
    long suffix_row_bytes = img->line_stride_bytes > 0
        ? (full_line_stride - band_record_stride * (long)img->bands) /
              img->suffix_band_items
        : assumed_suffix_row_bytes;

    long seek_pos = img->data_offset
                    + (long)row * full_line_stride
                    + band_record_stride * (long)img->bands
                    + pfx
                    + (long)suffix_index * suffix_row_bytes;

    /* Suffix items are LSB_SIGNED_INTEGER, suffix_item_bytes wide, in
     * every real archive seen so far (Cassini VIMS, MEX OMEGA) -- their
     * own SAMPLE_SUFFIX_ITEM_TYPE/BAND_SUFFIX_ITEM_TYPE keyword is not
     * yet parsed since it has never disagreed with this default. */
    long suffix_row_width = suffix_row_bytes / sfx_bpp;
    uint8_t *raw = (uint8_t *)G_malloc((size_t)suffix_row_width * sfx_bpp);
    if (fseek(fp, seek_pos, SEEK_SET) != 0 ||
        fread(raw, sfx_bpp, suffix_row_width, fp) != (size_t)suffix_row_width) {
        G_warning(_("p_pds: read error at row %d, suffix band %d"),
                   row, suffix_index);
        G_free(raw);
        return -1;
    }
    int host_is_le = p_pds_is_little_endian();
    for (int s = 0; s < ns; s++) {
        uint8_t *p = raw + (size_t)s * sfx_bpp;
        if (!host_is_le)
            p_pds_swap_bytes(p, 1, sfx_bpp);
        int32_t v;
        memcpy(&v, p, sizeof(v));
        buf[s] = (double)v;
    }
    G_free(raw);
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
