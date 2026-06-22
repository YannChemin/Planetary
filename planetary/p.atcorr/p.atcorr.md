## DESCRIPTION

*p.atcorr* is a generic, body-aware atmospheric correction dispatcher for
rocky solar-system bodies. Unlike *p.atcorr.hapke*, which corrects a whole
scene against one operator-supplied scalar optical depth (τ), *p.atcorr*
**retrieves a per-pixel atmospheric state from specific diagnostic bands
already present in the input cube**, then applies a body-appropriate
correction strategy with that per-pixel state — because real atmospheric
opacity (dust, haze, cloud) is not spatially uniform, and a real
hyperspectral/multispectral cube usually carries the information needed to
retrieve it directly, the same way Earth-observation tools retrieve AOD or
water vapour from a handful of narrow bands rather than assuming one
scene-wide value.

Each supported body carries a `regime` in `matter_bands.json`'s
`body_meta.<body>.atmosphere` block, which selects one of three strategies:

| Regime | Bodies | Strategy |
|---|---|---|
| `none` | Mercury, Moon | Passthrough — no scattering/absorbing column exists (surface-bound exosphere only). |
| `thin` | Mars | Per-pixel τ retrieved from a registered diagnostic gas absorption band (continuum-removed band depth → calibrated τ-proxy), then applied via a small τ-bin interpolation table built from the existing *p.atcorr.hapke* engine. |
| `thick` | Venus | No global radiative-transfer correction applies (cloud-top τ ≈ 25–40 in the visible makes direct surface imaging physically impossible outside specific instrument-engineered windows). Each registered window band is normalised per-pixel against a paired always-opaque reference band, removing local cloud-top opacity variation. Bands with no registered window are written NULL — refusing is the correct behaviour, not a fixed scalar "correction" with no physical basis. |

### Why per-pixel, not one global scalar (the `thin` regime)

*p.atcorr.hapke* takes one τ for the entire scene. *p.atcorr* instead:

1. Computes the continuum-removed band depth of the body's registered
   diagnostic gas feature (e.g. Mars's CO₂ 2.01 µm band) using the *exact
   same* Clark & Roush (1984) linear-continuum formula `p.matter.bands`
   already uses for mineral detection — reused via direct import, not
   reimplemented.
2. Converts that band depth to a τ-proxy via the database's `k_ref`
   calibration coefficient, clipped to the body's `[tau_clear, tau_dusty]`
   range; pixels with an out-of-range band depth fall back to `tau_clear`.
3. Evaluates *p.atcorr.hapke* once per band at `tau_bins=` discrete τ
   values spanning `[tau_clear, tau_dusty]`, then linearly interpolates
   each pixel's correction between its two bracketing bins — a small,
   single-axis version of the trilinear-LUT pattern used by per-pixel
   AOD/H₂O retrieval tools (e.g. `i.hyper.atcorr`'s `aod_map=`), built from
   the already-validated Hapke/Chandrasekhar engine instead of a new
   radiative-transfer model.

This means more *p.atcorr.hapke* calls than the single-tau path
(`tau_bins × n_bands`); see NOTES for the cost.

### Why Venus is not "thin atmosphere with a bigger number" (the `thick` regime)

The single-scattering Hapke/Chandrasekhar formula in `p_atmosmodel` assumes
an optically thin-to-moderate atmosphere; it is not valid at Venus's
cloud-top optical depths. Real Venus missions instead **design the sensor
around known narrow atmospheric windows** — Akatsuki IR2 and Venus Express
VIRTIS-M both carry channels at 1.01–1.18, 1.31, 1.51 and 2.3 µm
specifically because the deep atmosphere is more transparent there. *Even
inside those windows*, published near-IR surface studies (e.g. Mueller et
al. 2008) still normalise per-pixel against a simultaneous, always-opaque
reference band to remove local cloud-top altitude/opacity variation — the
same "diagnostic-band-pair → per-pixel atmospheric state → correction"
pattern as the `thin` regime, just with the diagnostic pair pre-selected by
the instrument designers rather than retrieved from a generic band set.
*p.atcorr* implements exactly that per-pixel ratio normalisation for each
registered window; it does not attempt a full multiple-scattering inversion
of the cloud deck.

## NOTES

### Cost of the `thin` regime

Each band is corrected by `tau_bins` (default 5) separate *p.atcorr.hapke*
invocations, so a 438-band CRISM-style cube with the default bin count
issues 2190 module calls. Lower `tau_bins=` for faster, coarser correction;
raise it for finer τ resolution at proportional cost.

### Diagnostic-band registry

The Mars τ-proxy retrieval and the Venus window/reference pairs both live
in `matter_bands.json`: Mars's `bodies.mars.gases[name=co2_atm].retrieval`
block, and Venus's `body_meta.venus.atmosphere.atmosphere_windows` list.
Both are additive extensions of the existing per-species/per-body schema
already used by `p.matter.bands` — no new top-level database file.

The Mars `k_ref` calibration coefficient is an order-of-magnitude
placeholder (documented in its own `notes` field) pending calibration
against a known-clear reference scene; treat `tau_proxy` as
relative/qualitative until calibrated, and inspect it directly with the
**-m** flag before trusting the corrected output quantitatively.

### Not implemented (out of scope for this version)

Wavelength-dependent τ(λ) via an Ångström exponent, joint AOD+H₂O optimal
estimation, adjacency-effect correction, and BRDF/polarisation correction
are all real techniques used by mature Earth-observation atmospheric
correction tools (e.g. `i.hyper.atcorr`, which this module's retrieval
*pattern* — not code — is informed by) but are not implemented here. This
is a single-axis τ-proxy retrieval with a small interpolation table, scoped
to the bodies and diagnostic bands this repo's database currently
documents.

## EXAMPLES

Mars, thin regime — requires `p.phocube` geometry:

```sh
p.phocube body=mars output=geom
p.atcorr input=crism_mawrth_ir body=mars \
    incidence=geom_incidence emission=geom_emission phase=geom_phase \
    tau_bins=5 -m output=mawrth_corrected
```

Venus, thick regime — window/reference-ratio normalisation only, no
geometry rasters needed:

```sh
p.atcorr input=venus_virtis body=venus window_tolerance=0.03 \
    -m output=venus_corrected
```

Mercury or Moon — passthrough:

```sh
p.atcorr input=messenger_mdis body=mercury output=mdis_passthrough
```

## REFERENCES

- Clark, R.N. & Roush, T.L. (1984). Reflectance spectroscopy: Quantitative
  analysis techniques for remote sensing applications. *JGR* 89(B7).

- Mueller, N. et al. (2008). Venus surface thermal emission at 1 µm in
  VIRTIS imaging observations: Evidence for variation of crust and mantle
  differentiation conditions. *Planetary and Space Science* 56(6).
  <https://doi.org/10.1016/j.pss.2008.04.010>

- Smith, M.D. (2004). Interannual variability in TES atmospheric
  observations of Mars during 1999–2003. *Icarus* 167(1).
  <https://doi.org/10.1029/2003JE002084>

## SEE ALSO

*[p.atcorr.hapke](p.atcorr.hapke.md),
[p.matter.bands](p.matter.bands.md),
[p.phocube](p.phocube.md)*

## AUTHOR

Yann Chemin
