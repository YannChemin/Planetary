## DESCRIPTION

*p.photrim* masks out pixels where the photometric geometry is outside
physically valid limits. Pixels where incidence angle ≥ 90° (beyond
the terminator) or emission angle ≥ 90° (beyond the limb) are set to
GRASS NULL. Additionally, pixels whose phase angle falls outside the
user-specified range [**min_phase**, **max_phase**] are masked.

This step is typically applied after *p.photomet* to remove artefacts
at terminator and limb areas where the photometric correction diverges.

## NOTES

If incidence or emission backplane rasters are not provided, only the
phase angle trimming is applied. All three geometry rasters (from
*p.phocube*) should be used together for best results.

## EXAMPLES

Trim terminator and limb pixels, restrict phase to 0–70°:

```sh
p.photrim input=ctx_phocorr \
    incidence=ctx_incidence emission=ctx_emission phase=ctx_phase \
    min_phase=0 max_phase=70 output=ctx_trimmed
```

### ISIS3-equivalent workflow

ISIS3's `photrim` masks pixels by per-pixel illumination / emission /
phase angles, identically to this module. To replicate

```
phocube  from=ctx.cub  to=ctx_cube.cub  incidence=true emission=true phase=true
photrim  from=ctx.cub  to=ctx_trim.cub  cubefile=ctx_cube.cub \
         minemission=0  maxemission=80 \
         minincidence=0 maxincidence=80 \
         minphase=0     maxphase=70
```

in GRASS, the equivalent chain is:

```sh
# 1. ISIS3 .cub -> GRASS raster
p.in.isis input=ctx.cub output=ctx
# 2. Backplanes (replaces ISIS3 phocube)
p.phocube -i -e -p input=ctx output=ctx
# 3. Trim by angles (replaces ISIS3 photrim)
p.photrim input=ctx \
    incidence=ctx_incidence emission=ctx_emission phase=ctx_phase \
    min_incidence=0 max_incidence=80 \
    min_emission=0  max_emission=80 \
    min_phase=0     max_phase=70 \
    output=ctx_trim
# 4. (optional) export the trimmed raster back to ISIS3 .cub
p.out.isis input=ctx_trim output=ctx_trim.cub
```

The output `ctx_trim` carries the same masking semantics as ISIS3's
`ctx_trim.cub`: pixels outside the requested angular envelope become
GRASS NULL (mapped to ISIS3 NULL on export).

## REFERENCES

- McEwen, A.S. (1991). Photometric functions for photoclinometry and
  other applications. *Icarus* 92:298–311.
  doi:[10.1016/0019-1035(91)90053-V](https://doi.org/10.1016/0019-1035(91)90053-V)

## SEE ALSO

*[p.phocube](p.phocube.md),
[p.photomet](p.photomet.md)*

## AUTHOR

Yann Chemin
