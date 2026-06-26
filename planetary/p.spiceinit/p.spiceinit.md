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

Attach kernels and observation metadata to a HiRISE RDR/COG product
(already georeferenced — `-s` flat-field mode works directly):

```sh
p.spiceinit map=hirise_red target=MARS observer=MRO \
    time=2007-01-05T01:26:56 \
    lsk=naif0012.tls pck=pck00010.tpc \
    sclk=MRO_SCLKSCET.00094.65536.tsc \
    ik=mro_hirise_v12.ti fk=mro_v17.tf \
    spk=mro_psp2.bsp,mar063.bsp \
    ck=mro_sc_psp_070102_070108.bc
p.phocube -s -iep input=hirise_red output=hirise_geom
# → hirise_geom_incidence, hirise_geom_emission, hirise_geom_phase
```

Attach kernels with per-scan-line timing (pushbroom sensor — each row
acquired at a slightly different time) and a DSK shape model (Phobos,
an irregular small body where the ellipsoid is a poor approximation):

```sh
p.spiceinit map=phobos_img target=PHOBOS observer=MRO \
    time=2008-03-23T12:45:00 line_rate=0.001 \
    lsk=naif0012.tls pck=pck00010.tpc \
    spk=mar097.bsp ck=mro_sc.bc \
    dsk=phobos_3_3.bds
p.phocube -s -iepr input=phobos_img output=phobos_geom
# local_radius and incidence/emission/phase now come from the real
# Phobos shape, not a smooth ellipsoid (~1.8 km surface deviation).
```

Attach kernels for a raw MEX OMEGA SWIR-C cube (whiskbroom, camera mode):

```sh
# omega= downloads the .QUB to ~/RSDATA/Mars/; mirror_dn imported separately
p.in.archive omega=orb0100_0 output=omega
p.in.pds3 input=~/RSDATA/Mars/ORB0100_0.QUB output=omega_mirror_dn suffix_band=1
g.region raster=omega.1
p.spiceinit map=omega.1 target=MARS observer=-41 \
    time=2004-02-10T18:08:35 line_rate=0.401 \
    lsk=naif0012.tls sclk=MEX_260522_STEP.TSC \
    ik=MEX_OMEGA_V03.TI fk=MEX_V16.TF \
    pck=MARS_IAU2000_V0.TPC,pck00010.tpc \
    spk=MEX_ROB_040101_041231_003.BSP,de432s.bsp,mar099.bsp \
    ck=ATNM_MEASURED_040101_050101_V03.BC
p.phocube -c -tn instrument=OMEGA_SWIR_C input=omega.1 \
    output=omega_geom mirror_dn=omega_mirror_dn
```

Attach kernels for a raw Cassini VIMS cube (2-axis scan, camera mode):

```sh
p.in.archive vims=titan_v1799424623 vims_channel=ir output=vims_titan_ir
# p.in.archive already writes sampling_mode_ir, x_offset, z_offset,
# swath_width, swath_length into vims_titan_ir.1's planetary.json.
g.region raster=vims_titan_ir.1
p.spiceinit map=vims_titan_ir.1 target=TITAN observer=CASSINI \
    time=2015-008T15:09:40.135 \
    lsk=naif0012.tls sclk=cas00172.tsc \
    ik=cas_vims_v06.ti,vimsAddendum04.ti fk=cas_v43.tf \
    pck=cpck_rock_21Jan2011_merged.tpc,pck00010.tpc \
    spk=150108AP_SCPSE_14365_15016.bsp ck=15008_15013ra.bc
p.phocube -c -tn instrument=VIMS_IR input=vims_titan_ir.1 output=vims_ir_geom
```

Attach kernels for a raw Cassini ISS NAC frame of Saturn (2-D framing
camera, camera mode; IAK contains per-filter focal length):

```sh
p.spice.find spacecraft=CASSINI instrument=ISS_NAC \
    time=2004-169T16:24:48 kernels=lsk,sclk,ik,fk,pck,spk,ck,iak \
    dest=~/RSDATA/Saturn/kernels
p.in.archive opus_id=co-iss-n1466182140 output=iss_nac
g.region raster=iss_nac
p.spiceinit map=iss_nac target=SATURN observer=CASSINI \
    time=2004-169T16:24:48.262 \
    lsk=~/RSDATA/Saturn/kernels/lsk/naif0012.tls \
    sclk=~/RSDATA/Saturn/kernels/sclk/cas00172.tsc \
    ik=~/RSDATA/Saturn/kernels/ik/cas_iss_v10.ti,~/RSDATA/Saturn/kernels/iak/IssNAAddendum005.ti \
    fk=~/RSDATA/Saturn/kernels/fk/cas_v43.tf \
    pck=~/RSDATA/Saturn/kernels/pck/cpck_rock_21Jan2011_merged.tpc,~/RSDATA/Saturn/kernels/pck/pck00010.tpc \
    spk=~/RSDATA/Saturn/kernels/spk/*.bsp \
    ck=~/RSDATA/Saturn/kernels/ck/*.bc
p.phocube -c -ieptn instrument=ISS_NAC input=iss_nac output=iss_nac_geom
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
