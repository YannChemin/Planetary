## DESCRIPTION

*p.caminfo* reports real SPICE-derived camera geometry for a raw,
*p.spiceinit*'d planetary camera image. It evaluates the same
per-instrument camera model `p.phocube -c`/`p.cam2map -c` use at the
image's centre pixel and its four corners, plus body-wide quantities
that don't need a camera ray at all (sub-solar point, sub-spacecraft
point, solar distance).

Supported instruments: `CRISM_VNIR`, `CRISM_IR` (MRO/CRISM, 1-D
pushbroom pinhole); `ISS_NAC`, `ISS_WAC` (Cassini ISS, 2-D framing
pinhole with K1 radial distortion and per-filter-pair focal length);
`OMEGA_SWIR_C`, `OMEGA_SWIR_L`, `OMEGA_VNIR` (MEX OMEGA, whiskbroom
scanning mirror — requires `mirror_dn=` raster); `VIMS_IR`, `VIMS_VIS`
(Cassini VIMS, 2-axis angular scan — requires `sampling_mode=` and
swath metadata).

For OMEGA, `mirror_dn=` must be a raster imported from the QUBE's
band-suffix sideplane via `p.in.pds3 suffix_band=1`. For VIMS,
`sampling_mode=`, `x_offset=`, `z_offset=`, `swath_width=`, and
`swath_length=` can be omitted when the raster was imported via
`p.in.archive vims=` (which writes them to the raster's
`planetary.json` metadata).

Reported quantities:

- Centre and four corner pixels: real camera-ray surface intercept
  (planetocentric latitude/longitude), via `sincpt`
- Illumination at centre: solar incidence, emission, phase angles
  (`ilumin`)
- Sub-solar point: latitude/longitude of the point on the target
  nearest the Sun (`subslr`)
- Sub-spacecraft point: latitude/longitude of the point on the target
  nearest the observer (`subpnt`)
- Solar distance: distance from the Sun to the target body, in AU
- Pixel resolution: ground-sample distance at the centre pixel
  (instantaneous-FOV x range), in m/pixel
- North azimuth: clockwise angle from image "up" to true north at the
  centre pixel (approximates the local surface normal as the
  centre-to-spoint direction -- exact for a sphere, a small
  approximation for a flattened ellipsoid)

A pixel with no camera-ray surface intercept (e.g. a corner pointed off
the target's disk) is reported as a miss, not a fabricated value.

## EXAMPLES

Report geometry for a real Cassini ISS NAC frame of Saturn:

```sh
p.spiceinit map=iss_nac target=SATURN observer=CASSINI time=2004-169T16:24:48.262 \
    lsk=naif0012.tls sclk=cas00172.tsc ik=cas_iss_v10.ti,IssNAAddendum005.ti \
    fk=cas_v43.tf pck=cpck_rock_21Jan2011_merged.tpc,pck00010.tpc \
    spk=040615AP_SCPSE_04167_04186.bsp ck=04168_04171ra.bc

p.caminfo input=iss_nac instrument=ISS_NAC filter1=P0 filter2=CB2
```

Save metadata as JSON:

```sh
p.caminfo -j input=hirise_red instrument=CRISM_VNIR > hirise_red_caminfo.json
```

## NOTES

This module works on the raw, un-georeferenced camera image (real
sample/line indices), the same convention `p.phocube -c`/`p.cam2map -c`
use -- not a map-projected product. The `input=` option name matches
`p.phocube`/`p.cam2map`'s own convention.

## REFERENCES

- Acton, C.H. (1996). Ancillary data services of NASA's NAIF.
  *Planetary and Space Science* 44(1):65-70.
  doi:[10.1016/0032-0633(95)00107-7](https://doi.org/10.1016/0032-0633(95)00107-7)

## SEE ALSO

*[p.spiceinit](p.spiceinit.md),
[p.phocube](p.phocube.md),
[p.cam2map](p.cam2map.md)*

## AUTHOR

Yann Chemin
