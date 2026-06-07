/*!
 * \file p_spectra.c
 *
 * \brief Planetary library - spectral analysis (implementation).
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#define _POSIX_C_SOURCE 200809L

#include "p_spectra.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef P_SPECTRA_STANDALONE
static void *G_malloc(size_t n)  { return malloc(n); }
static void *G_realloc(void *p, size_t n) { return realloc(p, n); }
static void  G_free(void *p)     { free(p); }
static void  G_warning(const char *fmt, ...) {
    va_list ap;
    #include <stdarg.h>
    va_start(ap, fmt);
    fprintf(stderr, "WARNING: "); vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
}
#  define _(s) (s)
#else
#  include <grass/gis.h>
#  include <grass/glocale.h>
#endif

/* ================================================================== */
/* Internal band entry                                                  */
/* ================================================================== */

typedef struct {
    double wavelength;  /* centre wavelength                  */
    double width;       /* FWHM bandwidth (0 if not provided) */
} PSpecBand;

/* ================================================================== */
/* Concrete PSpectraDef                                                 */
/* ================================================================== */

/*
 * Multi-section layout (matches ISIS3 SpectralDefinition1D):
 * Bands 0…(n0-1) → section 0
 * Bands n0…(n0+n1-1) → section 1
 * …
 * section_start[i] = index of first band in section i (0-based)
 */

struct PSpectraDef {
    int         nbands;          /* total bands across all sections */
    PSpecBand  *bands;           /* array [nbands]                  */
    int         nsections;       /* number of wavelength sections   */
    int        *section_start;   /* [nsections]: first band of each */
};

/* ================================================================== */
/* Construction helpers                                                 */
/* ================================================================== */

/* Detect section boundaries: a new section starts when wavelengths
 * reverse the direction established in section 0.
 * Returns heap array of section start indices (caller frees). */
static int detect_sections(const double *wavelengths, int n,
                            int **section_starts_out)
{
    if (n < 2) {
        int *s = (int *)G_malloc(sizeof(int));
        s[0] = 0;
        *section_starts_out = s;
        return 1;
    }

    /* Direction of the first pair. */
    int ascending = (wavelengths[1] > wavelengths[0]) ? 1 : 0;

    int  cap  = 8;
    int *secs = (int *)G_malloc((size_t)cap * sizeof(int));
    int  nsec = 0;
    secs[nsec++] = 0;

    int cur_ascending = ascending;
    for (int i = 2; i < n; i++) {
        int now_asc = (wavelengths[i] > wavelengths[i-1]) ? 1 : 0;
        if (now_asc != cur_ascending) {
            /* Direction reversed → new section. */
            if (nsec >= cap) {
                cap *= 2;
                secs = (int *)G_realloc(secs, (size_t)cap * sizeof(int));
            }
            secs[nsec++] = i;
            cur_ascending = now_asc;
        }
    }

    *section_starts_out = secs;
    return nsec;
}

/* ================================================================== */
/* Public construction API                                              */
/* ================================================================== */

PSpectraDef *p_spectra_def_create(int nbands,
                                   const double *wavelengths,
                                   const double *widths)
{
    if (nbands <= 0 || !wavelengths) {
        G_warning(_("p_spectra_def_create: nbands must be > 0 and wavelengths must not be NULL"));
        return NULL;
    }

    struct PSpectraDef *sd = (struct PSpectraDef *)G_malloc(sizeof(*sd));
    sd->nbands = nbands;
    sd->bands  = (PSpecBand *)G_malloc((size_t)nbands * sizeof(PSpecBand));

    for (int i = 0; i < nbands; i++) {
        sd->bands[i].wavelength = wavelengths[i];
        sd->bands[i].width      = widths ? widths[i] : 0.0;
    }

    int *sec_starts;
    sd->nsections     = detect_sections(wavelengths, nbands, &sec_starts);
    sd->section_start = sec_starts;

    return (PSpectraDef *)sd;
}

PSpectraDef *p_spectra_def_read_csv(const char *csv_path)
{
    FILE *fp = fopen(csv_path, "r");
    if (!fp) {
        G_warning(_("p_spectra_def_read_csv: cannot open '%s': %s"),
                   csv_path, strerror(errno));
        return NULL;
    }

    int    cap = 128;
    double *wl  = (double *)G_malloc((size_t)cap * sizeof(double));
    double *wd  = (double *)G_malloc((size_t)cap * sizeof(double));
    int     n   = 0;
    char    line[1024];

    while (fgets(line, sizeof(line), fp)) {
        /* Skip blank lines and comments. */
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        double w1, w2;
        if (sscanf(p, "%lf,%lf", &w1, &w2) == 2) {
            if (n >= cap) {
                cap *= 2;
                wl = (double *)G_realloc(wl, (size_t)cap * sizeof(double));
                wd = (double *)G_realloc(wd, (size_t)cap * sizeof(double));
            }
            wl[n] = w1;
            wd[n] = w2;
            n++;
        }
    }
    fclose(fp);

    if (n < 1) {
        G_warning(_("p_spectra_def_read_csv: no valid rows in '%s'"), csv_path);
        G_free(wl); G_free(wd);
        return NULL;
    }

    PSpectraDef *sd = p_spectra_def_create(n, wl, wd);
    G_free(wl);
    G_free(wd);
    return sd;
}

void p_spectra_def_free(PSpectraDef *sd)
{
    if (!sd) return;
    struct PSpectraDef *s = (struct PSpectraDef *)sd;
    G_free(s->bands);
    G_free(s->section_start);
    G_free(s);
}

/* ================================================================== */
/* Accessors                                                            */
/* ================================================================== */

int p_spectra_nbands(const PSpectraDef *sd)
{
    return sd ? ((const struct PSpectraDef *)sd)->nbands : 0;
}

double p_spectra_wavelength(const PSpectraDef *sd, int b)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || b < 0 || b >= s->nbands) return NAN;
    return s->bands[b].wavelength;
}

double p_spectra_width(const PSpectraDef *sd, int b)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || b < 0 || b >= s->nbands) return NAN;
    return s->bands[b].width;
}

int p_spectra_section_count(const PSpectraDef *sd)
{
    return sd ? ((const struct PSpectraDef *)sd)->nsections : 0;
}

int p_spectra_section_of_band(const PSpectraDef *sd, int b)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || b < 0 || b >= s->nbands) return -1;
    /* Find the section whose range contains b. */
    for (int i = s->nsections - 1; i >= 0; i--) {
        if (b >= s->section_start[i]) return i;
    }
    return 0;
}

int p_spectra_find_band(const PSpectraDef *sd, double wl, int section)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || s->nbands == 0) return -1;
    if (section < 0 || section >= s->nsections) section = 0;

    /* Band range for this section. */
    int bstart = s->section_start[section];
    int bend   = (section + 1 < s->nsections)
                 ? s->section_start[section + 1]
                 : s->nbands;

    int    best_b    = bstart;
    double best_diff = fabs(s->bands[bstart].wavelength - wl);

    for (int b = bstart + 1; b < bend; b++) {
        double diff = fabs(s->bands[b].wavelength - wl);
        if (diff < best_diff) {
            best_diff = diff;
            best_b    = b;
        }
    }
    return best_b;
}

/* ================================================================== */
/* Spectral interpolation helper                                        */
/* Linearly interpolate spectrum value at wavelength wl,               */
/* finding the nearest-neighbour bands on either side.                 */
/* ================================================================== */

static double interp_at_wl(const struct PSpectraDef *sd,
                             const double *spectrum,
                             double wl, int section)
{
    int bstart = sd->section_start[section];
    int bend   = (section + 1 < sd->nsections)
                 ? sd->section_start[section + 1]
                 : sd->nbands;
    int nb = bend - bstart;
    if (nb <= 0) return NAN;

    /* Decide direction of the section (ascending or descending λ). */
    int ascending = (nb >= 2) ?
                    (sd->bands[bstart + 1].wavelength > sd->bands[bstart].wavelength)
                    : 1;

    /* Find bounding bands around wl. */
    int lo = -1, hi = -1;
    for (int b = bstart; b < bend; b++) {
        double lam = sd->bands[b].wavelength;
        if (ascending) {
            if (lam <= wl) lo = b;
            if (lam >= wl && hi < 0) hi = b;
        } else {
            if (lam >= wl) lo = b;
            if (lam <= wl && hi < 0) hi = b;
        }
    }

    if (lo < 0 && hi < 0) return NAN;
    if (lo < 0) return spectrum[hi];
    if (hi < 0) return spectrum[lo];
    if (lo == hi)  return spectrum[lo];

    double lam_lo = sd->bands[lo].wavelength;
    double lam_hi = sd->bands[hi].wavelength;
    double dlam   = lam_hi - lam_lo;
    if (fabs(dlam) < 1.0e-30) return spectrum[lo];

    double t = (wl - lam_lo) / dlam;
    return spectrum[lo] + t * (spectrum[hi] - spectrum[lo]);
}

/* ================================================================== */
/* Band depth                                                           */
/* ================================================================== */

double p_spectra_band_depth(const PSpectraDef *sd,
                             const double *spectrum,
                             double wl_center,
                             double wl_left,
                             double wl_right,
                             int section)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || !spectrum) return NAN;

    double R_left  = interp_at_wl(s, spectrum, wl_left,   section);
    double R_right = interp_at_wl(s, spectrum, wl_right,  section);
    double R_cen   = interp_at_wl(s, spectrum, wl_center, section);

    if (R_left != R_left || R_right != R_right || R_cen != R_cen) return NAN;

    /* Linear continuum at wl_center. */
    double dw = wl_right - wl_left;
    double Rc;
    if (fabs(dw) < 1.0e-30) {
        Rc = R_left;
    } else {
        double t = (wl_center - wl_left) / dw;
        Rc = R_left + t * (R_right - R_left);
    }

    if (Rc <= 0.0) return NAN;
    return 1.0 - R_cen / Rc;
}

/* ================================================================== */
/* Band ratio                                                           */
/* ================================================================== */

double p_spectra_band_ratio(const PSpectraDef *sd,
                             const double *spectrum,
                             double wl1, double wl2,
                             int section)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;
    if (!s || !spectrum) return NAN;

    double R1 = interp_at_wl(s, spectrum, wl1, section);
    double R2 = interp_at_wl(s, spectrum, wl2, section);

    if (R1 != R1 || R2 != R2 || fabs(R2) < 1.0e-30) return NAN;
    return R1 / R2;
}

/* ================================================================== */
/* Spectral Angle Mapper                                                */
/* ================================================================== */

double p_spectra_sam(int nbands, const double *s1, const double *s2)
{
    double dot = 0.0, n1 = 0.0, n2 = 0.0;
    int valid = 0;

    for (int b = 0; b < nbands; b++) {
        double v1 = s1[b], v2 = s2[b];
        if (v1 != v1 || v2 != v2) continue;  /* NaN skip */
        dot += v1 * v2;
        n1  += v1 * v1;
        n2  += v2 * v2;
        valid++;
    }
    if (valid < 2 || n1 < 1.0e-30 || n2 < 1.0e-30) return NAN;

    double c = dot / (sqrt(n1) * sqrt(n2));
    if (c >  1.0) c =  1.0;
    if (c < -1.0) c = -1.0;
    return acos(c);
}

/* ================================================================== */
/* Continuum removal                                                    */
/* ================================================================== */

void p_spectra_continuum_remove(const PSpectraDef *sd,
                                 const double *spectrum,
                                 int nbands,
                                 double wl_left,
                                 double wl_right,
                                 int section,
                                 double *output)
{
    const struct PSpectraDef *s = (const struct PSpectraDef *)sd;

    double R_left  = interp_at_wl(s, spectrum, wl_left,  section);
    double R_right = interp_at_wl(s, spectrum, wl_right, section);
    double dw      = wl_right - wl_left;

    for (int b = 0; b < nbands; b++) {
        double lam = s->bands[b].wavelength;

        /* Bands outside [wl_left, wl_right]: pass through. */
        double lo_wl = (wl_left  < wl_right) ? wl_left  : wl_right;
        double hi_wl = (wl_left  < wl_right) ? wl_right : wl_left;
        if (lam < lo_wl || lam > hi_wl) {
            output[b] = spectrum[b];
            continue;
        }

        double Rc;
        if (fabs(dw) < 1.0e-30) {
            Rc = R_left;
        } else {
            double t = (lam - wl_left) / dw;
            Rc = R_left + t * (R_right - R_left);
        }

        if (Rc <= 0.0 || Rc != Rc) {
            output[b] = NAN;
        } else {
            output[b] = spectrum[b] / Rc;
        }
    }
}

/* ================================================================== */
/* Running-mean helper (symmetric window, NaN-aware)                   */
/* ================================================================== */

/* Compute running mean of spectrum[b-half … b+half], skipping NaN.
 * Returns NaN if fewer than 1 valid sample. */
static double running_mean(const double *spec, int nb, int b, int half)
{
    int lo = b - half;
    int hi = b + half;
    if (lo < 0)    lo = 0;
    if (hi >= nb)  hi = nb - 1;

    double sum  = 0.0;
    int    cnt  = 0;
    for (int i = lo; i <= hi; i++) {
        if (spec[i] == spec[i]) { sum += spec[i]; cnt++; }
    }
    return (cnt > 0) ? sum / cnt : NAN;
}

/* ================================================================== */
/* Spectral highpass / divfilter                                        */
/* ================================================================== */

void p_spectra_highpass(const double *spectrum,
                         int nbands,
                         int window,
                         double *output)
{
    if (window < 1) window = 1;
    int half = window / 2;

    for (int b = 0; b < nbands; b++) {
        double mean = running_mean(spectrum, nbands, b, half);
        if (mean != mean || spectrum[b] != spectrum[b])
            output[b] = NAN;
        else
            output[b] = spectrum[b] - mean;
    }
}

void p_spectra_divfilter(const double *spectrum,
                          int nbands,
                          int window,
                          double *output)
{
    if (window < 1) window = 1;
    int half = window / 2;

    for (int b = 0; b < nbands; b++) {
        double mean = running_mean(spectrum, nbands, b, half);
        if (mean != mean || spectrum[b] != spectrum[b] || fabs(mean) < 1.0e-30)
            output[b] = NAN;
        else
            output[b] = spectrum[b] / mean;
    }
}

/* ================================================================== */
/* Row-level parallel operations                                        */
/* ================================================================== */

void p_spectra_apply_row_band_depth(const PSpectraDef *sd,
                                     int nsamples, int nbands,
                                     const double *spectra,
                                     double wl_center,
                                     double wl_left, double wl_right,
                                     int section,
                                     double *out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        out[s] = p_spectra_band_depth(sd, spectra + (size_t)s * nbands,
                                       wl_center, wl_left, wl_right, section);
    }
}

void p_spectra_apply_row_band_ratio(const PSpectraDef *sd,
                                     int nsamples, int nbands,
                                     const double *spectra,
                                     double wl1, double wl2,
                                     int section,
                                     double *out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        out[s] = p_spectra_band_ratio(sd, spectra + (size_t)s * nbands,
                                       wl1, wl2, section);
    }
}

void p_spectra_apply_row_sam(int nsamples, int nbands,
                              const double *spectra,
                              const double *reference,
                              double *out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        out[s] = p_spectra_sam(nbands,
                                spectra + (size_t)s * nbands,
                                reference);
    }
}

void p_spectra_apply_row_highpass(int nsamples, int nbands,
                                   const double *spectra,
                                   int window,
                                   double *out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        p_spectra_highpass(spectra + (size_t)s * nbands,
                            nbands, window,
                            out + (size_t)s * nbands);
    }
}

void p_spectra_apply_row_divfilter(int nsamples, int nbands,
                                    const double *spectra,
                                    int window,
                                    double *out)
{
    int s;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) private(s)
#endif
    for (s = 0; s < nsamples; s++) {
        p_spectra_divfilter(spectra + (size_t)s * nbands,
                             nbands, window,
                             out + (size_t)s * nbands);
    }
}
