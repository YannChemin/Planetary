## DESCRIPTION

*p.caminfo* reports real SPICE-derived camera geometry for a raw,
*p.spiceinit*'d planetary camera image. It evaluates the same
per-instrument pinhole camera model `p.phocube -c`/`p.cam2map -c` use
(boresight, pixel pitch, focal length, K1 radial distortion, read from
the same real ISIS3 instrument kernel/IAK) at the image's centre pixel
and its four corners, plus body-wide quantities that don't need a
camera ray at all (sub-solar point, sub-spacecraft point, solar
distance).

Only `CRISM_VNIR`, `CRISM_IR`, `ISS_NAC`, and `ISS_WAC` are currently
supported. MEX OMEGA (whiskbroom scanning mirror) and Cassini VIMS
(2-axis angular scan) need extra per-pixel inputs (a mirror-DN raster,
real swath offsets) not yet wired into this module's centre/corner
evaluation -- see `p.phocube -c` for those, and `TODO.md` for the
status of extending `p.caminfo` to them.

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
