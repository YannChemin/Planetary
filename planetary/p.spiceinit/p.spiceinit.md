## DESCRIPTION

*p.spiceinit* attaches SPICE (Spacecraft Planet Instrument C-matrix
Events) kernel paths, target body, observer/spacecraft name, and
observation time to a GRASS raster map by appending them to the
raster's history metadata (one `SPICE_<KEY>=<value>` line per item).
Once attached, this is read back automatically by *p.phocube*'s SPICE
mode (`-s`) so kernels and geometry parameters need not be specified
repeatedly.

SPICE kernels are binary or text files maintained by NASA's Navigation
and Ancillary Information Facility (NAIF). The kernel types are:

| Type | Content |
|---|---|
| LSK | Leap second kernel |
| SCLK | Spacecraft clock kernel |
| CK | Camera orientation (C-matrix) kernel |
| SPK | Spacecraft and target ephemeris |
| IK | Instrument kernel |
| FK | Frames kernel |
| PCK | Planetary constants kernel |
| DSK | Digital shape kernel — real (non-ellipsoid) shape model |

In addition to kernel paths, `target=` stores the target body name,
`observer=` stores the observer/spacecraft name as known to the loaded
kernels (e.g. `MRO`), and `time=` stores a single mid-scene UTC
observation epoch (ISO 8601, e.g. `2007-01-05T01:26:56`) — all three are
required for *p.phocube -s* to work. `line_rate=` (seconds per output
row) is optional: when given, *p.phocube -s* computes each row's own
ephemeris time relative to `time=` instead of reusing one constant epoch
for the whole scene — useful for scenes long enough that real spacecraft
motion during acquisition changes the geometry row-to-row. `dsk=` is
also optional: when a DSK kernel is attached, *p.phocube -s* uses the
real (non-ellipsoid) shape it describes instead of the ellipsoid
approximation. The module validates that each kernel file is readable
before attaching it; pass `-t` to additionally test-load every kernel
with CSPICE itself.

## NOTES

*p.spiceinit* uses the `p_spice` library which wraps NAIF CSPICE N0067.
CSPICE is not thread-safe; calls are serialised even when OpenMP is
active elsewhere in the suite.

Each kernel-type/target/observer/time entry is appended as its own
history line (`Rast_append_history`); registering kernels in multiple
separate `p.spiceinit` invocations on the same map accumulates entries
rather than overwriting them.

## EXAMPLES

Attach kernels and observation metadata to a HiRISE RDR/COG product:

```sh
p.spiceinit map=hirise_red target=MARS observer=MRO \
    time=2007-01-05T01:26:56 \
    lsk=/kernels/naif/generic/lsk/naif0012.tls \
    sclk=/kernels/mro/sclk/MRO_SCLKSCET.00094.65536.tsc \
    spk=/kernels/mro/spk/mro_cruise.bsp \
    ck=/kernels/mro/ck/mro_sc_psp_070109_070115.bc \
    ik=/kernels/mro/ik/mro_hirise_v12.ti \
    pck=/kernels/naif/generic/pck/pck00010.tpc

p.phocube -s -iep input=hirise_red output=hirise_geom
```

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's Navigation and
  Ancillary Information Facility. *Planetary and Space Science*
  44(1):65–70. doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

- Acton, C. et al. (2018). A look towards the future in the handling of
  space science mission geometry. *Planetary and Space Science*
  150:9–12. doi:[10.1016/j.pss.2017.02.013](https://doi.org/10.1016/j.pss.2017.02.013)

- NAIF SPICE Toolkit (CSPICE N0067).
  <https://naif.jpl.nasa.gov/naif/toolkit.html>

## SEE ALSO

*[p.phocube](p.phocube.md),
[p.cam2map](p.cam2map.md),
[p.caminfo](p.caminfo.md)*

## AUTHOR

Yann Chemin
