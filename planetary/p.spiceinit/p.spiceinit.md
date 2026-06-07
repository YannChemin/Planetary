## DESCRIPTION

*p.spiceinit* attaches SPICE (Spacecraft Planet Instrument C-matrix
Events) kernel paths to a GRASS raster map by storing them in the
raster's metadata history. Once attached, these kernel paths are
available to geometry-aware modules such as *p.phocube* and *p.cam2map*.

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
| DSK | Digital shape kernel |

Kernels are specified individually or via a meta-kernel (`.tm`) file
that lists multiple kernels. The module validates that each kernel file
exists before attaching it.

## NOTES

*p.spiceinit* uses the `p_spice` library which wraps NAIF CSPICE N0067.
CSPICE is not thread-safe; calls to `sincpt` and `ilumin` are
serialised even when OpenMP is active.

Kernel files can also be set via the `PLANETSPICE_PATH` environment
variable (colon-separated directory list).

## EXAMPLES

Attach kernels to a HiRISE EDR cube imported with p.in.isis:

```sh
p.spiceinit input=hirise_red \
    lsk=/kernels/naif/generic/lsk/naif0012.tls \
    sclk=/kernels/mro/sclk/MRO_SCLKSCET.00094.65536.tsc \
    spk=/kernels/mro/spk/mro_cruise.bsp \
    ck=/kernels/mro/ck/mro_sc_psp_070109_070115.bc \
    ik=/kernels/mro/ik/mro_hirise_v12.ti \
    pck=/kernels/naif/generic/pck/pck00010.tpc

p.phocube input=hirise_red output=hirise_geom
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
