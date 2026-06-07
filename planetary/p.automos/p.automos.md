## DESCRIPTION

*p.automos* creates a mosaic from multiple input GRASS rasters. For
each input raster, the overlap with neighbouring rasters is detected
and blended using distance-weighted feathering: pixels near the seam
receive a weighted average of the two contributing images, with weights
proportional to the distance from the nearest no-data boundary.

Priority ordering for overlapping pixels (when not feathering):
**nadir** (centre-of-image pixels preferred) or **latest** (last
image in the list preferred).

## NOTES

All input rasters must be in the same projection and resolution. Use
*r.proj* or *p.cam2map* to reproject before mosaicking. The
computational region is expanded to cover all inputs unless explicitly
set.

For very large mosaics (hundreds of images) use the GRASS
*r.patch* + *r.mblend* pipeline instead.

## EXAMPLES

Mosaic three CTX images with feathering:

```sh
p.automos input=ctx_001,ctx_002,ctx_003 \
    feather=50 output=ctx_mosaic
```

Nadir-priority mosaic without blending:

```sh
p.automos input=ctx_001,ctx_002,ctx_003 \
    priority=nadir output=ctx_mosaic_nadir
```

## REFERENCES

- Soille, P. & Pesaresi, M. (2002). Advances in mathematical
  morphology applied to geoscience and remote sensing.
  *IEEE Trans. Geosci. Remote Sensing* 40(9):2042–2055.
  doi:[10.1109/TGRS.2002.804618](https://doi.org/10.1109/TGRS.2002.804618)

## SEE ALSO

*[p.cam2map](p.cam2map.md),
[r.patch](https://grass.osgeo.org/grass-stable/manuals/r.patch.html),
[r.proj](https://grass.osgeo.org/grass-stable/manuals/r.proj.html)*

## AUTHOR

Yann Chemin
