## DESCRIPTION

*p.photoclinometry* refines a seed DEM using a single calibrated planetary
reflectance image and a user-selected photometric model. The method is
*shape-from-shading* (SFS), also called *photoclinometry* in the
planetary-science literature.

The module implements the Horn-Brooks (1986) iterative gradient-domain
algorithm: at each iteration the surface gradient field (p,q) is updated
so that the photometric equation is better satisfied at every pixel, then
the gradient field is integrated once at the end to recover the height
field.

## ALGORITHM

For a nadir-viewing geometry (sensor looking straight down), the predicted
normalised brightness at a pixel whose surface normal is derived from
gradients (p,q) is:

```
  cos(i) = (-p·sx - q·sy + sz) / sqrt(p² + q² + 1)
  cos(e) = 1               / sqrt(p² + q² + 1)
  f(p,q) = PhotoModel(g, i, e) / PhotoModel(0, 0, 0)
```

where *(sx,sy,sz)* is the unit sun vector in (East, North, Up) coordinates
and *g* is the (constant) phase angle *90° − sun_elevation*.

The Horn-Brooks update rule for each pixel:

```
  p_new = p̄ − fp · (f(p̄,q̄) − R) / (fp² + fq² + λ²)
  q_new = q̄ − fq · (f(p̄,q̄) − R) / (fp² + fq² + λ²)
```

where *p̄,q̄* are the four-neighbour averages of the gradient field,
*R = I_obs / albedo* is the target normalised brightness, and
*fp,fq* are numerical partial derivatives of *f* with respect to *p* and *q*.
*λ* (lambda) is the Horn regularisation weight.

After *niter* iterations, the gradient field is integrated to height by
two-pass row+column cumulative summation and averaged.  The mean elevation
is then shifted to match the mean of the seed DEM.

## PHOTOMETRIC MODELS

All models are provided by the `p_photomodel` library (same as *p.photomet*):

| Model | Description |
|---|---|
| `lambert` | Lambertian: f = cos(i) |
| `lommelseeliger` | Lommel-Seeliger: f = 2cos(i)/(cos(i)+cos(e)) |
| `lunarlambert` | Lunar-Lambert blend (parameter L) |
| `minnaert` | Minnaert: f = cos(i)·(cos(i)·cos(e))^(K−1) |
| `hapkehen` | Full Hapke (1981) with Henyey-Greenstein phase function |
| `hapkeleg` | Full Hapke with Legendre polynomial phase function |
| `lunarlambertmcewen` | McEwen (1991) polynomial-scaled Lunar-Lambert |

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `input=` | required | Calibrated reflectance raster (I/F or equivalent) |
| `output=` | required | Output refined DEM raster |
| `seed=` | optional | Seed DEM raster; flat surface if omitted |
| `albedo=` | required | Surface normal albedo (dimensionless, in (0, 1]) |
| `sunaz=` | required | Solar azimuth [degrees from North clockwise] |
| `sunelev=` | required | Solar elevation [degrees above horizon] |
| `model=` | minnaert | Photometric model |
| `k=` | 0.5 | Minnaert K exponent |
| `l=` | 1.0 | LunarLambert L mixing weight |
| `wh=` | 0.5 | Hapke single-scatter albedo |
| `hh=` | 0.0 | Hapke opposition surge width |
| `b0=` | 0.0 | Hapke opposition surge amplitude |
| `hg1=` | 0.0 | HapkeHen 1st asymmetry coefficient |
| `hg2=` | 0.0 | HapkeHen 2nd component weight |
| `bh=` | 0.0 | HapkeLeg Legendre b1 |
| `ch=` | 0.0 | HapkeLeg Legendre b2 |
| `theta=` | 0.0 | Hapke macroscopic roughness angle [degrees] |
| `niter=` | 50 | Number of Horn-Brooks iterations |
| `lambda=` | 0.1 | Horn regularisation weight |

## NOTES

- The input reflectance must be calibrated to I/F units. Produce it with
  *p.photomet* or from mission L2 products.
- The seed DEM sets low-frequency shape and integration boundary conditions.
  A coarse MOLA/LOLA DEM is strongly recommended for regional work.
- Solar azimuth convention: 0° = North, 90° = East (GRASS GIS standard,
  same as *r.sunmask* output).
- Computation is OpenMP-parallelised; set `OMP_NUM_THREADS` to control thread count.
- For Hapke models, published parameter sets for specific bodies are found in
  Hapke (2012), Domingue et al. (2016), and mission-specific literature.

## EXAMPLE

```sh
# Photometrically correct a CTX image
p.photomet \
    input=ctx_raw \
    output=ctx_reflectance \
    model=minnaert k=0.5 \
    incidence=ctx_inc emission=ctx_emi phase=ctx_pha

# Derive topography from the corrected image
g.region raster=ctx_reflectance

p.photoclinometry \
    input=ctx_reflectance \
    output=ctx_sfs_dem \
    seed=mola_seed \
    albedo=0.17 \
    sunaz=225 \
    sunelev=35 \
    model=minnaert \
    k=0.5 \
    niter=100 \
    lambda=0.05

r.colors map=ctx_sfs_dem color=srtm
d.shade shade=ctx_sfs_dem color=ctx_reflectance brighten=40
```

## REFERENCES

Horn, B.K.P. & Brooks, M.J. (1986). "The variational approach to shape from
shading." *Computer Vision, Graphics, and Image Processing*, 33(2), 174–208.

Kirk, R.L. (1987). "A fast finite-element algorithm for two-dimensional
photoclinometry." PhD thesis, California Institute of Technology.

Lohse, V., Heipke, C., & Kirk, R.L. (2006). "Derivation of planetary
topography using multiscale photoclinometry." *Planetary and Space Science*,
54(7), 661–674.

Hapke, B. (1981). "Bidirectional reflectance spectroscopy: 1. Theory."
*Journal of Geophysical Research: Solid Earth*, 86(B4), 3039–3054.

## SEE ALSO

*p.photomet*, *p.phocube*, *p.in.archive*, *r.slope.aspect*, *r.sunmask*

## AUTHOR

Yann Chemin
