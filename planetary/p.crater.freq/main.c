/****************************************************************************
 *
 * MODULE:       p.crater.freq
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Crater size-frequency distribution analysis and surface age
 *               estimation using the Neukum or Hartmann production functions.
 *
 *               Reads crater diameters from EITHER:
 *                 (a) A CSV file (default) - one diameter [km] per line, '#' comments OK
 *                 (b) A designated column of a GRASS vector map - typically
 *                     populated by p.crater (Df_pi, D_eq, ...) in metres,
 *                     converted to km automatically.
 *
 *               Then:
 *                 1. Bins craters into log-spaced diameter classes
 *                 2. Normalises by the mapped area [km^2]
 *                 3. Compares to the Neukum or Hartmann production function
 *                 4. Estimates the surface age [Ga]
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/vector.h>
#include <grass/dbmi.h>
#include <grass/glocale.h>

/* Neukum production function (NPF) for the Moon, Mars, Mercury, Vesta.
 * log10(N) = a0 + sum_k(a_k * log10(D)^k)  for D in km.
 * Coefficients from Neukum et al. 2001 (Moon) and Hartmann 2005 (Mars).
 */

/* NPF Moon coefficients (Neukum 1983, up to a10) */
static const double NPF_MOON_A[] = {
    -3.0876, -3.557528, 0.781027, 1.021521, -0.156012,
    -0.444058, 0.019977, 0.086850, -0.005874, -0.006809, 0.000825
};
#define NPF_MOON_N 11

/* NPF Mars coefficients (Ivanov 2001) */
static const double NPF_MARS_A[] = {
    -3.383, -3.977, 0.674, 0.777, -0.097, -0.272, 0.009, 0.042, 0.0, 0.0, 0.0
};
#define NPF_MARS_N 11

/* NPF Mercury: same polynomial shape as Moon but a0 shifted by +0.40 to
 * match the N(D>=1km) production rate from Neukum et al. 2001 / Strom &
 * Neukum 1988 (Mercury surface crater density ~ 2.5x Moon at equal age).
 * Reference: Neukum G. et al. 2001, Space Sci. Rev. 96:55-86.           */
static const double NPF_MERCURY_A[] = {
    -2.688, -3.557528, 0.781027, 1.021521, -0.156012,
    -0.444058, 0.019977, 0.086850, -0.005874, -0.006809, 0.000825
};
#define NPF_MERCURY_N 11

/* NPF Vesta: Moon polynomial shape scaled down by one decade in N(1 km)
 * to approximate the lower main-belt impactor flux at Vesta/Ceres.
 * Reference: Schmedemann N. et al. 2014, Planet. Space Sci. 103:104-130.
 * (Their "lunar-like chronology" scaled by the Vesta flux factor ~0.1.)  */
static const double NPF_VESTA_A[] = {
    -4.088, -3.557528, 0.781027, 1.021521, -0.156012,
    -0.444058, 0.019977, 0.086850, -0.005874, -0.006809, 0.000825
};
#define NPF_VESTA_N 11

/* Select NPF coefficients and array length for the named body.
 * Returns 0 if body is unknown (caller should fall back to Moon).      */
static int npf_for_body(const char *body,
                         const double **a_out, int *n_out)
{
    if (!body) goto fallback;
    if (strcasecmp(body, "moon")    == 0) { *a_out=NPF_MOON_A;    *n_out=NPF_MOON_N;    return 1; }
    if (strcasecmp(body, "mars")    == 0) { *a_out=NPF_MARS_A;    *n_out=NPF_MARS_N;    return 1; }
    if (strcasecmp(body, "mercury") == 0) { *a_out=NPF_MERCURY_A; *n_out=NPF_MERCURY_N; return 1; }
    if (strcasecmp(body, "vesta")   == 0) { *a_out=NPF_VESTA_A;   *n_out=NPF_VESTA_N;   return 1; }
fallback:
    *a_out=NPF_MOON_A; *n_out=NPF_MOON_N;
    return 0;
}

static double npf_logN(const double *a, int n, double D_km)
{
    double logD = log10(D_km), logN = 0.0, logDp = 1.0;
    for (int k = 0; k < n; k++, logDp *= logD)
        logN += a[k] * logDp;
    return logN;
}

/* Neukum 1983 Moon chronology function (forward: age [Ga] -> N1 [km^-2]).
 * N(D>=1km) = 5.44e-14 * (exp(6.93 * t) - 1) + 8.38e-4 * t
 * Reference: Neukum G. 1983, Habilitationsschrift, Univ. Munich.
 * Used to invert N1_obs to age for the Moon Hartmann-style path.       */
static double neukum_N1_from_age(double age_Ga)
{
    return 5.44e-14 * (exp(6.93 * age_Ga) - 1.0) + 8.38e-4 * age_Ga;
}

/* Invert Neukum chronology by bisection: find t such that N1(t) = N1_obs. */
static double neukum_age_from_N1(double N1_obs)
{
    if (N1_obs <= 0.0) return 0.0;
    double lo = 0.0, hi = 4.5;
    for (int iter = 0; iter < 80; iter++) {
        double mid = 0.5 * (lo + hi);
        if (neukum_N1_from_age(mid) < N1_obs) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

/* ------------------------------------------------------------------ */
/* Hartmann (2005) Martian isochron tabulated values.                   */
/*                                                                      */
/* Reference: Hartmann, W. K. (2005), "Martian cratering 8: Isochron    */
/* refinement and the chronology of Mars", Icarus 174:294-320.          */
/*                                                                      */
/* Values are N(>=D) per km^2 at the given age, on a 16-point diameter  */
/* grid (log-spaced from 11 m to ~32 km). The tabulated isochrons are   */
/* a piecewise representation of Hartmann's "iteration 7" production    */
/* function for Mars across multiple chronology epochs.                 */
/*                                                                      */
/* For other bodies, the Hartmann mode falls back to the Neukum NPF     */
/* (which is the conventional choice anyway for Moon, Mercury, Vesta).  */
/* ------------------------------------------------------------------ */

#define HARTMANN_N_D   16
#define HARTMANN_N_AGE  8

/* Diameter bin centres [km] - log-spaced (steps of 0.5 in log10). */
static const double HARTMANN_D_KM[HARTMANN_N_D] = {
    0.011, 0.035, 0.110, 0.350, 1.10,  3.50,  11.0,  35.0,
    0.022, 0.070, 0.220, 0.700, 2.20,  7.00,  22.0,  70.0
};

/* Ages [Ga] for the canonical Mars isochron set. */
static const double HARTMANN_AGES[HARTMANN_N_AGE] = {
    0.001,  0.010,  0.100,  1.00,
    2.00,   3.00,   3.50,   4.00
};

/* N(>= D) [crater per km^2] at each (age, diameter) cell. Values
 * derived from Hartmann (2005) Fig.4/Table 1 logarithmic curves.    */
static const double HARTMANN_N[HARTMANN_N_AGE][HARTMANN_N_D] = {
    /* 0.001 Ga (1 Myr) */
    { 4.6e-2, 2.7e-3, 1.5e-4, 9.0e-6, 5.0e-7, 2.9e-8, 1.6e-9, 9.0e-11,
      1.5e-2, 8.3e-4, 4.5e-5, 2.7e-6, 1.5e-7, 8.5e-9, 4.7e-10, 2.6e-11 },
    /* 0.010 Ga (10 Myr) */
    { 4.6e-1, 2.7e-2, 1.5e-3, 9.0e-5, 5.0e-6, 2.9e-7, 1.6e-8, 9.0e-10,
      1.5e-1, 8.3e-3, 4.5e-4, 2.7e-5, 1.5e-6, 8.5e-8, 4.7e-9, 2.6e-10 },
    /* 0.100 Ga (100 Myr) */
    { 4.6e+0, 2.7e-1, 1.5e-2, 9.0e-4, 5.0e-5, 2.9e-6, 1.6e-7, 9.0e-9,
      1.5e+0, 8.3e-2, 4.5e-3, 2.7e-4, 1.5e-5, 8.5e-7, 4.7e-8, 2.6e-9 },
    /* 1.0 Ga */
    { 4.6e+1, 2.7e+0, 1.5e-1, 9.0e-3, 5.0e-4, 2.9e-5, 1.6e-6, 9.0e-8,
      1.5e+1, 8.3e-1, 4.5e-2, 2.7e-3, 1.5e-4, 8.5e-6, 4.7e-7, 2.6e-8 },
    /* 2.0 Ga - small-D branch saturates above this point (Mars
     * mid-history equilibrium for D < 100 m).                       */
    { 5.0e+1, 4.8e+0, 3.0e-1, 1.8e-2, 1.0e-3, 5.8e-5, 3.2e-6, 1.8e-7,
      2.5e+1, 1.5e+0, 9.0e-2, 5.4e-3, 3.0e-4, 1.7e-5, 9.4e-7, 5.2e-8 },
    /* 3.0 Ga - small-D fully saturated, large-D growth continues */
    { 5.0e+1, 5.0e+0, 4.5e-1, 2.7e-2, 1.5e-3, 8.7e-5, 4.8e-6, 2.7e-7,
      2.7e+1, 1.7e+0, 1.3e-1, 8.1e-3, 4.5e-4, 2.6e-5, 1.4e-6, 7.8e-8 },
    /* 3.5 Ga - Late Hesperian / Early Amazonian */
    { 5.0e+1, 5.0e+0, 5.0e-1, 6.0e-2, 3.4e-3, 2.0e-4, 1.1e-5, 6.0e-7,
      3.0e+1, 1.9e+0, 1.5e-1, 1.8e-2, 1.0e-3, 5.8e-5, 3.2e-6, 1.8e-7 },
    /* 4.0 Ga - Noachian heavy bombardment */
    { 5.0e+1, 5.0e+0, 5.0e-1, 5.0e-2, 1.5e-2, 8.7e-4, 4.8e-5, 2.7e-6,
      3.0e+1, 1.9e+0, 1.5e-1, 1.5e-2, 4.5e-3, 2.6e-4, 1.4e-5, 7.8e-7 }
};

/* Interpolate Hartmann isochron at (D_km, age_Ga). Linear in log-log. */
static double hartmann_N(double D_km, double age_Ga)
{
    if (D_km <= 0.0 || age_Ga <= 0.0) return 0.0;
    /* Find bracketing diameter cells. We just take the closest two
     * regardless of the non-monotonic storage order, using log-D    */
    double logD = log10(D_km);
    int    j0 = -1, j1 = -1;
    double d0 = 1e30, d1 = 1e30;
    for (int j = 0; j < HARTMANN_N_D; j++) {
        double dd = fabs(log10(HARTMANN_D_KM[j]) - logD);
        if (dd < d0) { d1 = d0; j1 = j0; d0 = dd; j0 = j; }
        else if (dd < d1) { d1 = dd; j1 = j; }
    }
    int i0 = 0;
    for (int i = 1; i < HARTMANN_N_AGE; i++)
        if (HARTMANN_AGES[i] <= age_Ga) i0 = i;
    int i1 = (i0 < HARTMANN_N_AGE - 1) ? i0 + 1 : i0;
    double t = (i1 == i0) ? 0.0 :
                 (log10(age_Ga) - log10(HARTMANN_AGES[i0])) /
                 (log10(HARTMANN_AGES[i1]) - log10(HARTMANN_AGES[i0]));
    /* Bilinear in (log D, log age) space. */
    double n00 = log10(HARTMANN_N[i0][j0]);
    double n01 = log10(HARTMANN_N[i0][j1]);
    double n10 = log10(HARTMANN_N[i1][j0]);
    double n11 = log10(HARTMANN_N[i1][j1]);
    double sd = d0 + d1;
    double wd1 = (sd > 0) ? (d0 / sd) : 0.5;
    double wd0 = 1.0 - wd1;
    double Ni0 = wd0 * n00 + wd1 * n01;
    double Ni1 = wd0 * n10 + wd1 * n11;
    double logN = (1.0 - t) * Ni0 + t * Ni1;
    return pow(10.0, logN);
}

/* Find the best-fit Hartmann age for an observed (D_km, N_obs) set
 * by minimum chi-square (least squares on log10(N)). Searches in
 * log-age space, bracketed by the tabulated ages.                  */
static double hartmann_fit_age(int n_bins,
                                const double *D_km, const double *N_obs)
{
    double best_age = 0.0;
    double best_chi2 = 1e300;
    /* 200 trial ages, log-spaced from 0.001 to 4.0 Ga. */
    for (int s = 0; s < 200; s++) {
        double age = pow(10.0, log10(0.001) +
                          s * (log10(4.0) - log10(0.001)) / 199.0);
        double chi2 = 0.0;
        int    used = 0;
        for (int i = 0; i < n_bins; i++) {
            if (N_obs[i] <= 0.0) continue;
            double N_pred = hartmann_N(D_km[i], age);
            if (N_pred <= 0.0) continue;
            double resid = log10(N_obs[i]) - log10(N_pred);
            chi2 += resid * resid;
            used++;
        }
        if (used > 0 && chi2 < best_chi2) {
            best_chi2 = chi2;
            best_age  = age;
        }
    }
    return best_age;
}

/* ------------------------------------------------------------------ */
/* Push a value into a growable double[] (G_realloc as needed).         */
/* ------------------------------------------------------------------ */
static void push_diameter(double **arr, int *n, int *cap, double d)
{
    if (*n >= *cap) {
        *cap *= 2;
        *arr = (double *)G_realloc(*arr, (size_t)(*cap) * sizeof(double));
    }
    (*arr)[(*n)++] = d;
}

/* ------------------------------------------------------------------ */
/* Read diameters from a CSV file (one per line, '#' comments).         */
/* Returns the number of valid diameters; *out is G_malloc'd by caller. */
/* ------------------------------------------------------------------ */
static int read_diameters_csv(const char *path, double **out)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        G_fatal_error(_("Cannot open crater CSV '%s'"), path);

    int cap = 1024, n = 0;
    double *arr = (double *)G_malloc((size_t)cap * sizeof(double));
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        double d = atof(line);
        if (d > 0.0) push_diameter(&arr, &n, &cap, d);
    }
    fclose(fp);
    *out = arr;
    return n;
}

/* ------------------------------------------------------------------ */
/* Read diameters from a column of a vector attribute table.            */
/* Values are auto-detected as metres (treated as such if max > 100) or */
/* kilometres (if max <= 100). All output is in km.                     */
/* ------------------------------------------------------------------ */
static int read_diameters_vector(const char *vector_name,
                                  const char *layer_name,
                                  const char *column_name,
                                  double **out)
{
    struct Map_info Map;
    Vect_set_open_level(2);
    if (Vect_open_old2(&Map, vector_name, "", layer_name) < 1)
        G_fatal_error(_("Unable to open vector map <%s>"), vector_name);

    int layer = Vect_get_field_number(&Map, layer_name);
    struct field_info *Fi = Vect_get_field(&Map, layer);
    if (!Fi) {
        Vect_close(&Map);
        G_fatal_error(_("Vector <%s> has no attribute table on layer %d"),
                      vector_name, layer);
    }

    dbDriver *driver = db_start_driver_open_database(Fi->driver, Fi->database);
    if (!driver) {
        Vect_close(&Map);
        G_fatal_error(_("Unable to open database <%s> by driver <%s>"),
                      Fi->database, Fi->driver);
    }
    db_set_error_handler_driver(driver);

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT %s FROM %s WHERE %s IS NOT NULL",
             column_name, Fi->table, column_name);
    dbString stmt;
    db_init_string(&stmt);
    db_set_string(&stmt, sql);

    dbCursor cursor;
    if (db_open_select_cursor(driver, &stmt, &cursor, DB_SEQUENTIAL) != DB_OK) {
        db_close_database_shutdown_driver(driver);
        Vect_close(&Map);
        G_fatal_error(_("Unable to execute query <%s> "
                         "(does column '%s' exist in table '%s'?)"),
                      sql, column_name, Fi->table);
    }

    int cap = 1024, n = 0;
    double *arr = (double *)G_malloc((size_t)cap * sizeof(double));
    dbTable *table = db_get_cursor_table(&cursor);
    int more;
    while (db_fetch(&cursor, DB_NEXT, &more) == DB_OK && more) {
        dbColumn *col = db_get_table_column(table, 0);
        dbValue  *val = db_get_column_value(col);
        if (db_test_value_isnull(val)) continue;

        double d = 0.0;
        int t = db_sqltype_to_Ctype(db_get_column_sqltype(col));
        if (t == DB_C_TYPE_INT)
            d = (double)db_get_value_int(val);
        else if (t == DB_C_TYPE_DOUBLE)
            d = db_get_value_double(val);
        else
            continue;  /* skip non-numeric */

        if (d > 0.0) push_diameter(&arr, &n, &cap, d);
    }
    db_close_cursor(&cursor);
    db_free_string(&stmt);
    db_close_database_shutdown_driver(driver);
    Vect_close(&Map);

    /* Auto-detect metres vs km: if any value > 100, assume metres
     * (CSFD studies rarely look at craters > 100 km). */
    double dmax = 0.0;
    for (int i = 0; i < n; i++) if (arr[i] > dmax) dmax = arr[i];
    if (dmax > 100.0) {
        G_message(_("Diameter column '%s' has max=%.0f, treating as metres "
                     "and converting to km."), column_name, dmax);
        for (int i = 0; i < n; i++) arr[i] *= 1e-3;
    } else {
        G_message(_("Diameter column '%s' has max=%.2f, treating as km."),
                  column_name, dmax);
    }

    *out = arr;
    return n;
}

/* ================================================================== */
int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_csv, *opt_vec, *opt_lyr, *opt_col;
    struct Option  *opt_area, *opt_body, *opt_output;
    struct Option  *opt_dmin, *opt_dmax, *opt_nbins;
    struct Flag    *flag_hartmann;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Crater Analysis"));
    G_add_keyword(_("crater"));
    G_add_keyword(_("chronology"));
    G_add_keyword(_("size-frequency"));
    module->label = _("Crater size-frequency distribution and surface age "
                       "estimation.");
    module->description =
        _("Reads crater diameters from a CSV file (default) OR from a "
          "designated column of a GRASS vector map (typically the output "
          "of p.crater), bins them into log-spaced diameter classes, "
          "normalises by the mapped area, compares to the Neukum or "
          "Hartmann production function, and estimates the surface "
          "age in Ga. CSV format: one crater diameter [km] per line, "
          "lines starting with '#' are skipped. Vector-column values "
          "are auto-detected as metres (if max > 100) or km.");

    /* --- input sources (exactly one required) --- */
    opt_csv = G_define_option();
    opt_csv->key         = "input";
    opt_csv->type        = TYPE_STRING;
    opt_csv->required    = NO;
    opt_csv->description = _("CSV file of crater diameters [km], "
                               "one per line. Default input source.");
    opt_csv->gisprompt   = "old,file,input";

    opt_vec = G_define_standard_option(G_OPT_V_INPUT);
    opt_vec->key         = "vector";
    opt_vec->required    = NO;
    opt_vec->description = _("Input crater vector map (alternative to "
                               "input=). Use together with column=.");

    opt_lyr = G_define_standard_option(G_OPT_V_FIELD);
    opt_lyr->required    = NO;
    opt_lyr->answer      = "1";

    opt_col = G_define_option();
    opt_col->key         = "column";
    opt_col->type        = TYPE_STRING;
    opt_col->required    = NO;
    opt_col->description = _("Attribute column holding crater diameters "
                               "(typically Df_pi, D_eq or Dat_pi from "
                               "p.crater). Required if vector= is given.");

    /* --- analysis parameters --- */
    opt_area = G_define_option();
    opt_area->key         = "area";
    opt_area->type        = TYPE_DOUBLE;
    opt_area->required    = YES;
    opt_area->description = _("Mapped area [km^2]");

    opt_body = G_define_option();
    opt_body->key         = "body";
    opt_body->type        = TYPE_STRING;
    opt_body->required    = NO;
    opt_body->answer      = "mars";
    opt_body->options     = "moon,mars,mercury,vesta";
    opt_body->description = _("Target body for production function. "
                               "Moon: Neukum 1983. Mars: Ivanov 2001. "
                               "Mercury: Neukum et al. 2001 (scaled Moon). "
                               "Vesta: Schmedemann et al. 2014 (scaled Moon).");

    opt_output = G_define_option();
    opt_output->key         = "output";
    opt_output->type        = TYPE_STRING;
    opt_output->required    = NO;
    opt_output->description = _("Output CSV: diameter bin, N_obs, N_npf");

    opt_dmin = G_define_option();
    opt_dmin->key         = "dmin";
    opt_dmin->type        = TYPE_DOUBLE;
    opt_dmin->required    = NO;
    opt_dmin->answer      = "0.1";
    opt_dmin->description = _("Minimum diameter bin [km]");

    opt_dmax = G_define_option();
    opt_dmax->key         = "dmax";
    opt_dmax->type        = TYPE_DOUBLE;
    opt_dmax->required    = NO;
    opt_dmax->answer      = "100.0";
    opt_dmax->description = _("Maximum diameter bin [km]");

    opt_nbins = G_define_option();
    opt_nbins->key         = "nbins";
    opt_nbins->type        = TYPE_INTEGER;
    opt_nbins->required    = NO;
    opt_nbins->answer      = "18";
    opt_nbins->description = _("Number of log-spaced diameter bins "
                                 "(default: 3 per decade)");

    flag_hartmann = G_define_flag();
    flag_hartmann->key         = 't';
    flag_hartmann->description = _("Use Hartmann (2005) isochron tables "
                                     "instead of NPF (note: '-h' is "
                                     "reserved for --help)");

    if (G_parser(argc, argv))
        exit(EXIT_FAILURE);

    /* ---- validate input sources: exactly one of csv|vector ---- */
    int have_csv = (opt_csv->answer && opt_csv->answer[0]);
    int have_vec = (opt_vec->answer && opt_vec->answer[0]);
    if (have_csv == have_vec) {
        G_fatal_error(_("Exactly one of input= (CSV) or vector= "
                         "must be given (got %s)"),
                      have_csv ? "both" : "neither");
    }
    if (have_vec && !(opt_col->answer && opt_col->answer[0])) {
        G_fatal_error(_("vector= requires column= "
                         "(name of the diameter attribute)"));
    }

    double area_km2 = atof(opt_area->answer);
    double dmin     = atof(opt_dmin->answer);
    double dmax     = atof(opt_dmax->answer);
    int    nbins    = atoi(opt_nbins->answer);
    const char *body_name = opt_body->answer;
    const double *npf_a;
    int           npf_n;
    if (!npf_for_body(body_name, &npf_a, &npf_n))
        G_warning(_("Unknown body '%s' - falling back to Moon NPF."), body_name);

    /* ---- read diameters ---- */
    double *diameters = NULL;
    int ncraters = 0;
    if (have_csv) {
        ncraters = read_diameters_csv(opt_csv->answer, &diameters);
        G_message(_("Read %d craters from CSV '%s'"),
                  ncraters, opt_csv->answer);
    } else {
        ncraters = read_diameters_vector(opt_vec->answer,
                                          opt_lyr->answer,
                                          opt_col->answer,
                                          &diameters);
        G_message(_("Read %d craters from vector <%s> column '%s'"),
                  ncraters, opt_vec->answer, opt_col->answer);
    }

    if (ncraters == 0) {
        G_free(diameters);
        G_fatal_error(_("No valid crater diameters found"));
    }

    /* Log-spaced bins */
    double logdmin = log10(dmin), logdmax = log10(dmax);
    double *bin_centers = (double *)G_malloc((size_t)nbins * sizeof(double));
    int    *bin_counts  = (int    *)G_malloc((size_t)nbins * sizeof(int));
    for (int i = 0; i < nbins; i++) {
        bin_centers[i] = pow(10.0,
                              logdmin + (i + 0.5) * (logdmax - logdmin) / nbins);
        bin_counts[i] = 0;
    }

    for (int ci = 0; ci < ncraters; ci++) {
        double d = diameters[ci];
        if (d < dmin || d > dmax) continue;
        int bi = (int)((log10(d) - logdmin) / (logdmax - logdmin) * nbins);
        if (bi < 0) bi = 0;
        if (bi >= nbins) bi = nbins - 1;
        bin_counts[bi]++;
    }

    /* Cumulative N (>= D) per area */
    double *N_cum = (double *)G_malloc((size_t)nbins * sizeof(double));
    long total = ncraters;
    long cumsum = 0;
    for (int i = 0; i < nbins; i++) {
        cumsum += bin_counts[i];
        N_cum[i] = (double)(total - cumsum + bin_counts[i]) / area_km2;
    }

    /* NPF reference and surface-age estimate */
    double N1_ref = pow(10.0, npf_logN(npf_a, npf_n, 1.0));
    double N1_obs = 0.0;
    for (int i = 0; i < nbins; i++)
        if (bin_centers[i] >= 1.0) { N1_obs = N_cum[i]; break; }
    double age_Ga = (N1_ref > 1e-20) ? N1_obs / N1_ref : 0.0;

    /* If -t requested, use a body-appropriate chronology. */
    if (flag_hartmann->answer) {
        if (strcasecmp(body_name, "moon") == 0) {
            /* Moon: Neukum 1983 chronology function inversion.
             * Derive N(>=1km) from the observed CSFD, then invert t.  */
            double age_h = neukum_age_from_N1(N1_obs);
            if (age_h > 0.0) {
                G_message(_("  Neukum 1983 Moon chronology: age = %.3f Ga"),
                          age_h);
                age_Ga = age_h;
            } else {
                G_warning(_("Neukum 1983 inversion returned zero age - "
                             "keeping NPF ratio estimate"));
            }
        } else if (strcasecmp(body_name, "mars") == 0) {
            /* Mars: Hartmann 2005 chi-square fit against tabulated
             * isochrons (Hartmann, Icarus 174:294-320).               */
            double age_h = hartmann_fit_age(nbins, bin_centers, N_cum);
            if (age_h > 0.0) {
                G_message(_("  Hartmann 2005 Mars isochron best-fit age: "
                             "%.3f Ga"), age_h);
                age_Ga = age_h;
            } else {
                G_warning(_("Hartmann 2005 fit returned no valid age - "
                             "keeping NPF estimate"));
            }
        } else {
            G_warning(_("No Hartmann-style isochron table available for "
                         "body '%s'; keeping NPF ratio estimate. "
                         "Use body=moon or body=mars for a "
                         "calibrated chronology function."), body_name);
        }
    }

    /* ---- Power-law least-squares fit:
     *      log10(N_cum) = a + b * log10(D_km)         (b is negative)
     * over bins with at least one crater and within [dmin, dmax].   */
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int    nfit = 0;
    for (int i = 0; i < nbins; i++) {
        if (bin_counts[i] <= 0 || N_cum[i] <= 0.0) continue;
        double x = log10(bin_centers[i]);
        double y = log10(N_cum[i]);
        sx  += x;   sy  += y;
        sxx += x*x; sxy += x*y;
        nfit++;
    }
    double slope = 0.0, intercept = 0.0, rmse = 0.0;
    if (nfit >= 2) {
        double denom = nfit * sxx - sx * sx;
        if (fabs(denom) > 1e-20) {
            slope     = (nfit * sxy - sx * sy) / denom;
            intercept = (sy - slope * sx) / nfit;
            double sse = 0.0;
            for (int i = 0; i < nbins; i++) {
                if (bin_counts[i] <= 0 || N_cum[i] <= 0.0) continue;
                double x = log10(bin_centers[i]);
                double y = log10(N_cum[i]);
                double yhat = intercept + slope * x;
                sse += (y - yhat) * (y - yhat);
            }
            rmse = sqrt(sse / nfit);
        }
    }

    G_message(_("Results for %s (area=%.1f km^2):"),
              opt_body->answer, area_km2);
    G_message(_("  Craters: %d   N(D>=1km) = %.4e km^-2"),
              ncraters, N1_obs);
    G_message(_("  Power-law fit: log10(N) = %.4f + %.4f * log10(D)   "
                 "rmse_log10=%.4f  (n=%d bins used)"),
              intercept, slope, rmse, nfit);
    G_message(_("  Estimated surface age: %.3f Ga%s"),
              age_Ga,
              flag_hartmann->answer
                ? " (Hartmann 2005 isochron chi^2 best-fit)"
                : " (Neukum 1983/Ivanov 2001 NPF ratio)");

    FILE *out_fp = opt_output->answer ? fopen(opt_output->answer, "w") : stdout;
    if (!out_fp && opt_output->answer)
        G_warning(_("Cannot write output '%s'"), opt_output->answer);
    if (out_fp) {
        const char *src = have_csv ? opt_csv->answer : opt_vec->answer;
        fprintf(out_fp, "# Crater SFD analysis\n");
        fprintf(out_fp, "# body=%s  area=%.3f km^2  N=%d  age=%.4f Ga\n",
                opt_body->answer, area_km2, ncraters, age_Ga);
        fprintf(out_fp, "# Power-law fit log10(N)=%.6f%+.6f*log10(D)  "
                          "rmse_log10=%.6f  nfit=%d\n",
                intercept, slope, rmse, nfit);
        fprintf(out_fp, "# Source: %s\n", src);
        fprintf(out_fp, "#\n");
        fprintf(out_fp, "# D_km  n_in_bin  N_cum_obs  N_npf_age  "
                          "N_powerlaw_fit\n");
        for (int i = 0; i < nbins; i++) {
            double D     = bin_centers[i];
            double N_npf = pow(10.0, npf_logN(npf_a, npf_n, D))
                              * N1_obs / N1_ref;
            double N_pl  = (nfit >= 2)
                              ? pow(10.0, intercept + slope * log10(D))
                              : 0.0;
            fprintf(out_fp,
                    "%.6f  %d  %.6e  %.6e  %.6e\n",
                    D, bin_counts[i], N_cum[i], N_npf, N_pl);
        }
        if (opt_output->answer) fclose(out_fp);
    }

    G_free(diameters);
    G_free(bin_centers);
    G_free(bin_counts);
    G_free(N_cum);
    return EXIT_SUCCESS;
}
