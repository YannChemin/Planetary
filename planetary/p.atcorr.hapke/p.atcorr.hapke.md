## DESCRIPTION

*p.atcorr.hapke* applies atmospheric correction to a planetary raster
using one of four Hapke radiative-transfer models from the `p_atmosmodel`
library. The correction removes the contribution of atmospheric scattering
and absorption, yielding a surface-leaving radiance factor I/F.

Atmospheric models:

| Model | Description |
|---|---|
| **Isotropic1** | Isotropic scattering, first-order approximation |
| **Isotropic2** | Isotropic scattering, Chandrasekhar H-function (second order) |
| **Anisotropic1** | Anisotropic (Henyey-Greenstein) scattering, first order |
| **Anisotropic2** | Anisotropic scattering, H-function (second order) |

Parameters:

- **tau** — Normal optical depth of atmosphere (τ)
- **wha** — Single-scatter albedo of atmospheric haze particles (ωₐ)
- **hga** — Henyey-Greenstein asymmetry factor for haze (gₐ, Anisotropic models)
- **bha** — Backscatter fraction (bₐ, Anisotropic models)
- **hnorm** — Normalisation atmospheric depth (h_norm)

## NOTES

The En and Ei integral tables are computed once per (tau, wha) pair and
cached; changing only geometry parameters does not trigger recomputation.

For Mars, typical values are τ ≈ 0.3–1.5 (dust storm), ωₐ ≈ 0.9.

## EXAMPLES

Apply Isotropic2 atmospheric correction to a MOC-NA image:

```sh
p.atcorr.hapke input=moc_na_dn \
    incidence=moc_incidence emission=moc_emission \
    model=Isotropic2 tau=0.5 wha=0.92 output=moc_atcorr
```

## REFERENCES

- Chandrasekhar, S. (1960). *Radiative Transfer*. Dover Publications.
  ISBN 0-486-60590-6.

- Hapke, B. (1993). *Theory of Reflectance and Emittance Spectroscopy*.
  Cambridge University Press. ISBN 0-521-30789-9.

- Ahmad, S.P. & Franz, G.W. (1984). Atmospheric effects on remote
  sensing. In: *Remote Sensing Yearbook*, pp. 31–61. Butterworths.

## SEE ALSO

*[p.photomet](p.photomet.md),
[p.albedo](p.albedo.md)*

## AUTHOR

Yann Chemin
