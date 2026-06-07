## DESCRIPTION

*p.crater.freq* analyses crater size-frequency distributions (CSFDs)
on a planetary surface and estimates a model surface age using the
Neukum production function (Moon, Mars, Mercury, Vesta).

Crater diameters can be supplied from EITHER of two sources:

- **`input=file.csv`** (default) - a CSV file with one crater diameter
  in km per line. Lines starting with `#` are ignored.
- **`vector=mymap` + `column=Df_pi`** - a column in a GRASS vector
  attribute table. This is the natural follow-on from *p.crater*,
  which writes the final crater diameter to the `Df_pi` column (in
  metres). Values larger than 100 are auto-detected as metres and
  converted to km.

The cumulative size-frequency N(>=D) is computed in log-spaced
diameter bins, then compared to the Neukum production function (NPF)
evaluated at the bin centres. The age is estimated from the ratio
of observed N(D >= 1 km) to the NPF value at 1 Ga.

An additional log-log least-squares **power-law fit**
`log10(N_cum) = intercept + slope * log10(D)` is computed over the
populated bins; the fit parameters and the per-bin RMSE are reported
in the run log and as a header in the output CSV.

### Output CSV columns

When `output=` is given, the file contains a header block followed by
five whitespace-separated columns:

| Column            | Meaning                                         |
|-------------------|-------------------------------------------------|
| `D_km`            | Bin centre diameter [km]                        |
| `n_in_bin`        | Raw crater count in this bin                    |
| `N_cum_obs`       | Cumulative observed density N(>=D) [km^-2]      |
| `N_npf_age`       | NPF curve at the estimated age, evaluated at D  |
| `N_powerlaw_fit`  | Least-squares power-law fit value at D          |

The header lines (prefixed by `#`) record the body, area, crater count,
estimated age and the fitted power-law slope, intercept and RMSE so the
file is self-describing.

## PRODUCTION FUNCTIONS AND CHRONOLOGIES

| Body    | NPF source                           | Chronology (`-t` flag)                      |
|---------|--------------------------------------|---------------------------------------------|
| Moon    | Neukum 1983 (full polynomial)        | Neukum 1983 chronology function (inversion) |
| Mars    | Ivanov 2001 (full polynomial)        | Hartmann 2005 isochron chi²-fit             |
| Mercury | Neukum et al. 2001 (scaled Moon)     | NPF ratio (warning; no per-body table)      |
| Vesta   | Schmedemann et al. 2014 (scaled Moon)| NPF ratio (warning; no per-body table)      |

**NPF ratio age** (default, no `-t`): age = N1_obs / N1_npf where N1_npf
is the per-Ga production at D = 1 km from the polynomial. This is a
linear approximation valid for ages < 3 Ga.

**Moon Neukum 1983 chronology** (`-t`): solves
`N(>=1km) = 5.44×10⁻¹⁴ (e^{6.93t} − 1) + 8.38×10⁻⁴ t`
for t by bisection. Accounts for the exponential bombardment tail in
the Late Heavy Bombardment epoch; more accurate than the NPF ratio
for ages > 3 Ga.

**Mars Hartmann 2005 isochrons** (`-t`): chi-square fit of the
observed cumulative CSFD against an 8-age × 16-diameter tabulated
isochron grid (Hartmann, *Icarus* 174:294-320, Table 1). Best for
multi-epoch martian terrain dating.

Ages < 0.1 Ga carry large uncertainties regardless of method.
`-h` is reserved by GRASS for `--help`.

## EXAMPLES

### From a CSV catalogue

Date a volcanic plain on Mars from a CSV of measured diameters:

```sh
p.crater.freq input=tharsis_craters.csv area=12500 body=mars \
    output=tharsis_csfd.csv
```

### From p.crater output

Use *p.crater* to compute final-diameter estimates on a mapped
crater rim vector, then date the surface from the *Df_pi* column:

```sh
p.crater -b body=moon \
    input=apollo16_rims output=apollo16_scaled \
    impactor_velocity=18000 impactor_density=3000

p.crater.freq vector=apollo16_scaled column=Df_pi \
    area=2400 body=moon \
    output=apollo16_csfd.csv
```

Diameter values are read in metres (auto-detected, then converted to
km).

## REFERENCES

- Neukum, G. (1983). *Meteoritenbombardement und Datierung planetarer
  Oberflaechen.* Habilitation thesis, LMU Munich.
  (Moon NPF polynomial + chronology function.)
- Neukum, G., Ivanov, B.A., & Hartmann, W.K. (2001). Cratering records
  in the inner solar system in relation to the lunar reference system.
  *Space Science Reviews* 96:55-86.
  [doi:10.1023/A:1011989004263](https://doi.org/10.1023/A:1011989004263)
  (Mercury NPF scaling.)
- Ivanov, B.A. (2001). Mars/Moon cratering rate ratio estimates.
  *Space Science Reviews* 96:87-104.
  [doi:10.1023/A:1011941121102](https://doi.org/10.1023/A:1011941121102)
  (Mars NPF polynomial.)
- Hartmann, W.K. & Neukum, G. (2001). Cratering chronology and the
  evolution of Mars. *Space Science Reviews* 96:165-194.
  [doi:10.1023/A:1011945222010](https://doi.org/10.1023/A:1011945222010)
- Hartmann, W.K. (2005). Martian cratering 8: Isochron refinement and
  the chronology of Mars. *Icarus* 174:294-320.
  [doi:10.1016/j.icarus.2004.11.023](https://doi.org/10.1016/j.icarus.2004.11.023)
  (Mars Hartmann 2005 isochron table.)
- Schmedemann, N. et al. (2014). The cratering record, chronology and
  surface ages of (4) Vesta in comparison to smaller asteroids and the
  ages of HED meteorites. *Planetary and Space Science* 103:104-130.
  [doi:10.1016/j.pss.2014.04.004](https://doi.org/10.1016/j.pss.2014.04.004)
  (Vesta NPF scaling.)

## NOTES

Surface-age estimates are only valid for bodies covered by the built-in Neukum production functions (Moon, Mars, Mercury, Vesta). Crater diameters must be supplied in km; CSV comment lines must use `#`. The module does not correct for crater resurfacing or secondary craters — these must be screened from the input population before running.

## SEE ALSO

*[p.crater](p.crater.md) - vector crater scaling that produces the
diameter columns consumed here,
[r.crater](https://grass.osgeo.org/grass-stable/manuals/addons/r.crater.html)*

## AUTHOR

Yann Chemin
