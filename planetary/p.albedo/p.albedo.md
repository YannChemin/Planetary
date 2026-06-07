## DESCRIPTION

*p.albedo* converts a calibrated I/F (radiance factor) raster to
geometric albedo or normal albedo using a photometric model from the
`p_photomodel` library. The geometric albedo is defined as the ratio of
the disk-integrated brightness at zero phase angle to that of a
Lambertian disk of the same cross-section:

```
A_geom = π · (I/F at i=e=g=0)
```

The photometric model must match the one used for calibration. Model
parameters (Hapke ω, h, B₀, θ̄; Minnaert k; LunarLambert L) are
specified as module options.

Supported models: Lambert, LommelSeeliger, LunarLambert, Minnaert,
HapkeHen, HapkeLeg, LunarLambertMcEwen.

## NOTES

For multi-band imagery (e.g. CRISM, OMEGA, VIMS), run *p.albedo*
separately for each band using the corresponding I/F band and the same
geometry backplanes.

## EXAMPLES

Compute normal albedo for a Lunar Reconnaissance Orbiter LROC NAC image:

```sh
p.albedo input=lroc_nac_if \
    incidence=lroc_incidence emission=lroc_emission phase=lroc_phase \
    model=LunarLambert l=0.5 output=lroc_albedo
```

## REFERENCES

- Hapke, B. (1993). *Theory of Reflectance and Emittance Spectroscopy*.
  Cambridge University Press. ISBN 0-521-30789-9.

- Shkuratov, Y. et al. (2011). Optical measurements of the Moon as a
  tool to study its surface. *Planet. Space Sci.* 59(13):1326–1371.
  doi:[10.1016/j.pss.2011.06.011](https://doi.org/10.1016/j.pss.2011.06.011)

## SEE ALSO

*[p.photomet](p.photomet.md),
[p.phocube](p.phocube.md),
[p.atcorr.hapke](p.atcorr.hapke.md)*

## AUTHOR

Yann Chemin
