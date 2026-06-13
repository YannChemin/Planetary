/*
 * spice-brief — summarise coverage of SPK and binary PCK kernel files.
 *
 * Equivalent to the NAIF 'brief' utility.  Prints each body ID (with name
 * when resolvable) and its time intervals in UTC calendar strings.
 *
 * Usage:
 *   spice-brief [-l lsk.tls] file1.bsp [file2.bsp ...]
 *
 * Without -l the endpoints are printed as ET seconds past J2000.
 *
 * Build: cc -o spice-brief spice-brief.c -I../../cspice-pkg/cspice/include \
 *            -L../../cspice-pkg/build -lcspice -Wl,-rpath,<build_dir> -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SpiceUsr.h"

/* Maximum bodies / intervals per file */
#define MAX_BODIES   2000
#define MAX_IVLS    200000

static void usage(void)
{
    fputs("Usage: spice-brief [-l lsk.tls] file.bsp [file2.bsp ...]\n"
          "  -l lsk   leapseconds kernel; enables UTC output\n", stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    int    have_lsk = 0;
    int    first_file = 1;

    /* Suppress CSPICE abort-on-error; we handle failures ourselves. */
    erract_c("SET", 256, "RETURN");
    errprt_c("SET", 256, "NONE");

    if (argc < 2) usage();

    /* Parse -l lsk before file arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) usage();
            furnsh_c(argv[++i]);
            if (failed_c()) { reset_c(); fprintf(stderr, "Warning: could not load LSK %s\n", argv[i]); }
            else have_lsk = 1;
        } else {
            /* First non-flag argument index */
            first_file = i;
            break;
        }
    }

    SPICEINT_CELL   (ids,   MAX_BODIES);
    SPICEDOUBLE_CELL(cover, MAX_IVLS);

    for (int fi = first_file; fi < argc; fi++) {
        if (argv[fi][0] == '-') continue;   /* skip stray flags */
        const char *fname = argv[fi];
        printf("\n=== %s ===\n", fname);

        /* Try SPK first */
        scard_c(0, &ids);
        spkobj_c(fname, &ids);
        if (failed_c()) { reset_c(); goto try_pck; }

        if (card_c(&ids) == 0) {
            printf("  (no objects found in SPK)\n");
            continue;
        }

        SpiceInt nobjs = card_c(&ids);
        for (SpiceInt i = 0; i < nobjs; i++) {
            SpiceInt body = SPICE_CELL_ELEM_I(&ids, i);

            SpiceChar bname[64] = "";
            SpiceBoolean found;
            bodc2n_c(body, 64, bname, &found);
            if (!found || bname[0] == '\0')
                snprintf(bname, sizeof(bname), "BODY %d", (int)body);

            scard_c(0, &cover);
            spkcov_c(fname, body, &cover);
            if (failed_c()) { reset_c(); printf("  %s: (coverage error)\n", bname); continue; }

            SpiceInt niv = wncard_c(&cover);
            for (SpiceInt j = 0; j < niv; j++) {
                SpiceDouble b, e;
                wnfetd_c(&cover, j, &b, &e);

                if (have_lsk) {
                    SpiceChar bstr[32], estr[32];
                    et2utc_c(b, "ISOC", 3, 32, bstr);
                    et2utc_c(e, "ISOC", 3, 32, estr);
                    printf("  %-30s  %s  to  %s\n", bname, bstr, estr);
                } else {
                    SpiceChar bstr[32], estr[32];
                    etcal_c(b, 32, bstr);
                    etcal_c(e, 32, estr);
                    printf("  %-30s  %s  to  %s\n", bname, bstr, estr);
                }
            }
        }
        continue;

try_pck:
        /* Try binary PCK */
        scard_c(0, &ids);
        pckfrm_c(fname, &ids);
        if (failed_c()) { reset_c(); printf("  (not a recognised SPK or binary PCK)\n"); continue; }

        SpiceInt nfrm = card_c(&ids);
        for (SpiceInt i = 0; i < nfrm; i++) {
            SpiceInt frm = SPICE_CELL_ELEM_I(&ids, i);
            scard_c(0, &cover);
            pckcov_c(fname, frm, &cover);
            if (failed_c()) { reset_c(); continue; }
            SpiceInt niv = wncard_c(&cover);
            for (SpiceInt j = 0; j < niv; j++) {
                SpiceDouble b, e;
                wnfetd_c(&cover, j, &b, &e);
                if (have_lsk) {
                    SpiceChar bstr[32], estr[32];
                    et2utc_c(b, "ISOC", 3, 32, bstr);
                    et2utc_c(e, "ISOC", 3, 32, estr);
                    printf("  FRAME %-24d  %s  to  %s\n", (int)frm, bstr, estr);
                } else {
                    SpiceChar bstr[32], estr[32];
                    etcal_c(b, 32, bstr);
                    etcal_c(e, 32, estr);
                    printf("  FRAME %-24d  %s  to  %s\n", (int)frm, bstr, estr);
                }
            }
        }
    }
    return 0;
}
