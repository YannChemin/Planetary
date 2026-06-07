## DESCRIPTION

*p.photomet* normalises a calibrated planetary raster to standard
photometric conditions (incidence i = emission e = phase g = 0°) using
per-pixel geometry backplanes produced by *p.phocube*. The correction
divides the measured radiance factor I/F by the photometric function
evaluated at the actual geometry and multiplies by the function
evaluated at the reference geometry.

Supported photometric models:

| Model | Formula (simplified) |
|---|---|
| **Lambert** | μ₀ / π |
| **LommelSeeliger** | μ₀ / (μ₀ + μ) |
| **LunarLambert** | L·μ₀/π + (1−L)·μ₀/(μ₀+μ) |
| **Minnaert** | A₀ · μ₀ᵏ · μᵏ⁻¹ |
| **HapkeHen** | Full Hapke with Henyey-Greenstein phase function |
| **HapkeLeg** | Full Hapke with Legendre polynomial phase function |
| **LunarLambertMcEwen** | Empirical lunar model (McEwen 1991) |

where μ₀ = cos(i), μ = cos(e), g = phase angle.

## NOTES

Hapke model parameters: **wh** (single-scatter albedo ω, 0–1),
**hh** (opposition surge width h), **b0** (opposition surge amplitude
B₀), **theta** (macroscopic roughness θ̄, degrees), **hg1**, **hg2**
(Henyey-Greenstein asymmetry parameters), **bh**, **ch** (Legendre
coefficients).

Pixels where μ₀ ≤ 0 or μ ≤ 0 (limb/terminator) are set to NULL.

## EXAMPLES

LunarLambert correction with mixing weight L=0.5:

```sh
p.photomet input=ctx_dn \
    incidence=ctx_incidence emission=ctx_emission phase=ctx_phase \
    model=LunarLambert l=0.5 output=ctx_ll
```

Full Hapke (Henyey-Greenstein) correction for a Mars surface:

```sh
p.photomet input=hrsc_nd3 \
    incidence=hrsc_i emission=hrsc_e phase=hrsc_g \
    model=HapkeHen wh=0.52 hh=0.06 b0=1.5 theta=20.0 \
    hg1=0.213 hg2=0.4 output=hrsc_hapke
```

## REFERENCES

- Hapke, B. (1981). Bidirectional reflectance spectroscopy 1. Theory.
  *J. Geophys. Res.* 86(B4):3039–3054.
  doi:[10.1029/JB086iB04p03039](https://doi.org/10.1029/JB086iB04p03039)

- Hapke, B. (1984). Bidirectional reflectance spectroscopy 3.
  Correction for macroscopic roughness. *Icarus* 59(1):41–59.
  doi:[10.1016/0019-1035(84)90054-X](https://doi.org/10.1016/0019-1035(84)90054-X)

- Hapke, B. (2002). Bidirectional reflectance spectroscopy 5.
  The coherent backscatter opposition effect. *Icarus* 157(2):523–534.
  doi:[10.1006/icar.2002.6853](https://doi.org/10.1006/icar.2002.6853)

- Minnaert, M. (1941). The reciprocity principle in lunar photometry.
  *Astrophysical Journal* 93:403–410.
  doi:[10.1086/144279](https://doi.org/10.1086/144279)

- Henyey, L.G. & Greenstein, J.L. (1941). Diffuse radiation in the
  galaxy. *Astrophysical Journal* 93:70–83.
  doi:[10.1086/144246](https://doi.org/10.1086/144246)

- McEwen, A.S. (1991). Photometric functions for photoclinometry and
  other applications. *Icarus* 92:298–311.
  doi:[10.1016/0019-1035(91)90053-V](https://doi.org/10.1016/0019-1035(91)90053-V)

## SEE ALSO

*[p.phocube](p.phocube.md),
[p.photrim](p.photrim.md),
[p.albedo](p.albedo.md),
[p.atcorr.hapke](p.atcorr.hapke.md)*

## AUTHOR

Yann Chemin
