/*
 * spice-ckbrief — summarise coverage of CK (pointing) kernel files.
 *
 * Equivalent to the NAIF 'ckbrief' utility.  Prints each instrument ID
 * and its pointing coverage intervals.
 *
 * Usage:
 *   spice-ckbrief [-l lsk.tls] [-s sclk.tsc -sc scid] file1.bc [...]
 *
 * -l lsk   leapseconds kernel; enables UTC calendar output
 * -s sclk  spacecraft clock kernel; enables SCLK string output
 * -sc id   spacecraft ID for SCLK output (integer, e.g. -82 for Cassini)
 *
 * Without -l/-s endpoints are printed as ET seconds past J2000.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SpiceUsr.h"

#define MAX_INSTR  2000
#define MAX_IVLS  200000

static void usage(void)
{
    fputs("Usage: spice-ckbrief [-l lsk] [-s sclk -sc scid] file.bc [...]\n"
          "  -l lsk    leapseconds kernel\n"
          "  -s sclk   spacecraft clock kernel\n"
          "  -sc id    spacecraft NAIF ID (e.g. -82 for Cassini)\n", stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    int have_lsk = 0, have_sclk = 0;
    SpiceInt sc_id = 0;
    int first_file = 1;

    erract_c("SET", 256, "RETURN");
    errprt_c("SET", 256, "NONE");

    if (argc < 2) usage();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) usage();
            furnsh_c(argv[++i]);
            if (failed_c()) { reset_c(); fprintf(stderr, "Warning: could not load LSK %s\n", argv[i]); }
            else have_lsk = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) usage();
            furnsh_c(argv[++i]);
            if (failed_c()) { reset_c(); fprintf(stderr, "Warning: could not load SCLK %s\n", argv[i]); }
            else have_sclk = 1;
        } else if (strcmp(argv[i], "-sc") == 0) {
            if (i + 1 >= argc) usage();
            sc_id = (SpiceInt)atoi(argv[++i]);
        } else {
            first_file = i;
            break;
        }
    }

    SPICEINT_CELL   (ids,   MAX_INSTR);
    SPICEDOUBLE_CELL(cover, MAX_IVLS);

    for (int fi = first_file; fi < argc; fi++) {
        if (argv[fi][0] == '-') continue;
        const char *fname = argv[fi];
        printf("\n=== %s ===\n", fname);

        scard_c(0, &ids);
        ckobj_c(fname, &ids);
        if (failed_c()) { reset_c(); printf("  (not a valid CK file)\n"); continue; }

        SpiceInt ninstr = card_c(&ids);
        if (ninstr == 0) { printf("  (no instruments found)\n"); continue; }

        for (SpiceInt i = 0; i < ninstr; i++) {
            SpiceInt instr = SPICE_CELL_ELEM_I(&ids, i);

            SpiceChar iname[64] = "";
            SpiceBoolean found;
            bodc2n_c(instr, 64, iname, &found);
            if (!found || iname[0] == '\0')
                snprintf(iname, sizeof(iname), "INSTR %d", (int)instr);

            scard_c(0, &cover);
            /* needav=SPICEFALSE, level="INTERVAL", tol=0, timsys="TDB" */
            ckcov_c(fname, instr, SPICEFALSE, "INTERVAL", 0.0, "TDB", &cover);
            if (failed_c()) { reset_c(); printf("  %s: (coverage error)\n", iname); continue; }

            SpiceInt niv = wncard_c(&cover);
            for (SpiceInt j = 0; j < niv; j++) {
                SpiceDouble b, e;
                wnfetd_c(&cover, j, &b, &e);

                if (have_lsk) {
                    SpiceChar bstr[32], estr[32];
                    et2utc_c(b, "ISOC", 3, 32, bstr);
                    et2utc_c(e, "ISOC", 3, 32, estr);
                    printf("  %-32s  %s  to  %s\n", iname, bstr, estr);
                } else if (have_sclk && sc_id != 0) {
                    SpiceChar bstr[64], estr[64];
                    sce2s_c(sc_id, b, 64, bstr);
                    sce2s_c(sc_id, e, 64, estr);
                    printf("  %-32s  %s  to  %s  (SCLK)\n", iname, bstr, estr);
                } else {
                    SpiceChar bstr[32], estr[32];
                    etcal_c(b, 32, bstr);
                    etcal_c(e, 32, estr);
                    printf("  %-32s  %s  to  %s\n", iname, bstr, estr);
                }
            }
        }
    }
    return 0;
}
