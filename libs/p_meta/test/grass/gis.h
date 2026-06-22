/*!
 * \file test/grass/gis.h
 * \brief Minimal GRASS GIS stubs for standalone unit testing of p_meta.
 *
 * Placed on the include path with -I./test so that #include <grass/gis.h>
 * in p_meta.c resolves to this file instead of the real GRASS header.
 * Redirects G_malloc / G_calloc / G_free to stdlib and silences
 * G_warning / G_verbose_message to stderr / noop respectively.
 *
 * NOT for production use.
 */
#ifndef GRASS_GIS_H
#define GRASS_GIS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* Memory */
#define G_malloc(n)     malloc((size_t)(n))
#define G_calloc(n, s)  calloc((size_t)(n), (size_t)(s))
#define G_free(p)       free(p)

/* Environment — read from process environment */
static inline const char *G_getenv(const char *key) { return getenv(key); }
static inline const char *G_getenv_nofatal(const char *key) { return getenv(key); }

/* Diagnostics */
static inline void _g_warning_stub(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "p_meta WARNING: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
#define G_warning         _g_warning_stub

static inline void _g_verbose_stub(const char *fmt, ...) { (void)fmt; }
#define G_verbose_message _g_verbose_stub

#endif /* GRASS_GIS_H */
