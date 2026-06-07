## DESCRIPTION

**p.spice.subpoint** prints the **sub-solar** and/or **sub-observer**
(typically sub-Earth) point of a body at a given UTC epoch, computed with the
adopted CSPICE library. It is both a teaching/debugging tool — to inspect the
exact geometry a particular time produces — and the shared SPICE backend that
*p.illumination.sunfraction* and *p.visibility.earth* call when run with
`ephemeris=spice`.

By default the target body, body-fixed frame, observer and meta-kernel are
taken from the mapset's SPICE configuration (set with *p.spice.config*); any of
them can be overridden on the command line.

Longitudes are reported east-positive, in the planetocentric convention of the
chosen body-fixed frame.

## PARAMETERS

| Parameter | Type | Default | Description |
|---|---|---|---|
| epoch | string | *required* | UTC epoch, ISO-8601 (e.g. `2028-06-01T00:00:00`) |
| point | string | both | `sun`, `observer`, or `both` |
| meta | file | mapset config | Meta-kernel to load |
| target | string | mapset config / MOON | Target body |
| frame | string | mapset config / IAU_MOON | Body-fixed frame |
| observer | string | mapset config / EARTH | Observer body for the sub-observer point |

## FLAGS

| Flag | Description |
|---|---|
| -g | Print in shell/script style (`key=value`) |

## EXAMPLES

### Sub-solar and sub-Earth point of the Moon (using mapset config)
```
p.spice.subpoint epoch=2028-06-01T00:00:00
```

### Script-style output for one epoch
```
p.spice.subpoint epoch=2028-06-01T00:00:00 point=sun -g
```

### Override target/frame without touching the mapset config
```
p.spice.subpoint epoch=2030-01-01T00:00:00 target=MARS frame=IAU_MARS \
    meta=$HOME/.grass8/p_spice/meta/mars.tm
```

## NOTES

The meta-kernel (`mk=`) must furnish LSK, PCK, SPK, and (if required) FK kernels for both the observer and the target body. NAIF SPICE error messages are forwarded directly to the GRASS error stream; consult the NAIF "SPICE Required Reading" documents for kernel diagnostics. Use *p.spice.config* to set the default meta-kernel for a mapset.

## SEE ALSO

*[p.in.spice](p.in.spice.md),
[p.spice.config](p.spice.config.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.visibility.earth](p.visibility.earth.md)*

## AUTHOR

Yann Chemin
