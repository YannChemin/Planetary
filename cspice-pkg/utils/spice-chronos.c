/*
 * spice-chronos — convert times between ET, UTC, and SCLK.
 *
 * Equivalent to the NAIF 'chronos' utility.
 *
 * Usage:
 *   spice-chronos -l lsk [-s sclk] [-sc id] -from FMT -to FMT TIME
 *
 * Input/output formats (-from, -to):
 *   UTC    ISO calendar UTC        "2004-183T03:11:40.288"
 *   ET     ephemeris time seconds  "141619900.471"
 *   SCLK   spacecraft clock tick   "1/1467344155.116"  (needs -s -sc)
 *   CAL    ET calendar string      "2004 JUL 01 03:11:40.471"
 *
 * Examples:
 *   spice-chronos -l naif0012.tls -from UTC -to ET "2004-183T03:11:40.288"
 *   spice-chronos -l naif0012.tls -s cas00172.tsc -sc -82 \
 *                 -from SCLK -to UTC "1/1467344155.116"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SpiceUsr.h"

static void usage(void)
{
    fputs(
        "Usage: spice-chronos -l lsk [-s sclk] [-sc id] -from FMT -to FMT TIME\n"
        "  Formats: UTC  ET  SCLK  CAL\n"
        "  -l lsk      leapseconds kernel (required)\n"
        "  -s sclk     spacecraft clock kernel (for SCLK format)\n"
        "  -sc id      spacecraft NAIF ID (e.g. -82 for Cassini)\n"
        "  -from FMT   input time format\n"
        "  -to FMT     output time format\n"
        "  TIME        time string to convert\n"
        "\nExample: spice-chronos -l naif0012.tls -from UTC -to ET "
        "\"2004-07-01T03:11:40\"\n", stderr);
    exit(1);
}

typedef enum { FMT_UTC, FMT_ET, FMT_SCLK, FMT_CAL, FMT_UNKNOWN } TimeFmt;

static TimeFmt parse_fmt(const char *s)
{
    if (strcasecmp(s, "UTC")  == 0) return FMT_UTC;
    if (strcasecmp(s, "ET")   == 0) return FMT_ET;
    if (strcasecmp(s, "SCLK") == 0) return FMT_SCLK;
    if (strcasecmp(s, "CAL")  == 0) return FMT_CAL;
    return FMT_UNKNOWN;
}

int main(int argc, char *argv[])
{
    const char *lsk = NULL, *sclk_file = NULL, *time_str = NULL;
    const char *from_s = NULL, *to_s = NULL;
    SpiceInt sc_id = 0;

    erract_c("SET", 256, "RETURN");
    errprt_c("SET", 256, "NONE");

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "-l")    == 0 && i+1 < argc) lsk       = argv[++i];
        else if (strcmp(argv[i], "-s")    == 0 && i+1 < argc) sclk_file = argv[++i];
        else if (strcmp(argv[i], "-sc")   == 0 && i+1 < argc) sc_id     = atoi(argv[++i]);
        else if (strcmp(argv[i], "-from") == 0 && i+1 < argc) from_s    = argv[++i];
        else if (strcmp(argv[i], "-to")   == 0 && i+1 < argc) to_s      = argv[++i];
        else if (argv[i][0] != '-')                            time_str  = argv[i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(); }
    }

    if (!lsk || !from_s || !to_s || !time_str) usage();

    furnsh_c(lsk);
    if (failed_c()) { fprintf(stderr, "Error: cannot load LSK %s\n", lsk); return 1; }

    if (sclk_file) {
        furnsh_c(sclk_file);
        if (failed_c()) { fprintf(stderr, "Error: cannot load SCLK %s\n", sclk_file); return 1; }
    }

    TimeFmt from_fmt = parse_fmt(from_s);
    TimeFmt to_fmt   = parse_fmt(to_s);
    if (from_fmt == FMT_UNKNOWN || to_fmt == FMT_UNKNOWN) {
        fprintf(stderr, "Unknown format (use UTC, ET, SCLK, or CAL)\n");
        return 1;
    }
    if ((from_fmt == FMT_SCLK || to_fmt == FMT_SCLK) && (sc_id == 0 || !sclk_file)) {
        fprintf(stderr, "SCLK format requires -s sclk and -sc id\n");
        return 1;
    }

    /* Convert input to ET */
    SpiceDouble et = 0.0;
    switch (from_fmt) {
        case FMT_UTC:
        case FMT_CAL:
            str2et_c(time_str, &et);
            break;
        case FMT_ET:
            et = atof(time_str);
            break;
        case FMT_SCLK:
            scs2e_c(sc_id, time_str, &et);
            break;
        default: break;
    }
    if (failed_c()) {
        SpiceChar msg[1840];
        getmsg_c("LONG", 1840, msg);
        fprintf(stderr, "Error converting input: %s\n", msg);
        return 1;
    }

    /* Convert ET to output format */
    SpiceChar out[128] = "";
    switch (to_fmt) {
        case FMT_UTC:
            et2utc_c(et, "ISOC", 3, 128, out);
            break;
        case FMT_ET:
            snprintf(out, sizeof(out), "%.6f", (double)et);
            break;
        case FMT_CAL:
            etcal_c(et, 128, out);
            break;
        case FMT_SCLK:
            sce2s_c(sc_id, et, 128, out);
            break;
        default: break;
    }
    if (failed_c()) {
        SpiceChar msg[1840];
        getmsg_c("LONG", 1840, msg);
        fprintf(stderr, "Error converting output: %s\n", msg);
        return 1;
    }

    printf("%s\n", out);
    return 0;
}
