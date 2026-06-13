/*
 * spice-commnt — read the comment area of any SPICE binary kernel.
 *
 * Equivalent to the read portion of the NAIF 'commnt' utility.
 *
 * Usage:
 *   spice-commnt file.bsp|file.bc|file.bpc [...]
 *
 * Prints the embedded comment records from the DAF or DAS comment area.
 * Useful for identifying kernel authorship, coverage, and applicability.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SpiceUsr.h"

#define BUFSZ  20000
#define LINSZ  1001

static void dump_daf_comments(const char *fname)
{
    SpiceInt handle = 0;
    dafopr_c(fname, &handle);
    if (failed_c()) { reset_c(); return; }

    SpiceChar buf[BUFSZ];
    SpiceInt  n = 0;
    SpiceBoolean done = SPICEFALSE;

    dafec_c(handle, BUFSZ / LINSZ, LINSZ, &n, buf, &done);
    if (failed_c()) { reset_c(); dafcls_c(handle); return; }

    /* buf contains n null-terminated lines packed into LINSZ-char slots */
    for (SpiceInt i = 0; i < n; i++)
        printf("%s\n", buf + (size_t)i * LINSZ);

    while (!done) {
        dafec_c(handle, BUFSZ / LINSZ, LINSZ, &n, buf, &done);
        if (failed_c()) { reset_c(); break; }
        for (SpiceInt i = 0; i < n; i++)
            printf("%s\n", buf + (size_t)i * LINSZ);
    }

    dafcls_c(handle);
    if (failed_c()) reset_c();
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fputs("Usage: spice-commnt file.bsp|file.bc|file.bpc [...]\n"
              "  Prints the comment area of DAF-based SPICE kernels\n"
              "  (SPK .bsp, CK .bc, binary PCK .bpc).\n", stderr);
        return 1;
    }

    erract_c("SET", 256, "RETURN");
    errprt_c("SET", 256, "NONE");

    for (int i = 1; i < argc; i++) {
        printf("=== %s ===\n", argv[i]);
        dump_daf_comments(argv[i]);
        if (failed_c()) {
            reset_c();
            fprintf(stderr, "  (could not read comments — not a DAF kernel?)\n");
        }
    }
    return 0;
}
