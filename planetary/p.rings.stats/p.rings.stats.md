## DESCRIPTION

*p.rings.stats* computes radial statistics of ring brightness from a
ring-plane projected raster (output of *p.rings.project*). Pixels are
binned into annular rings of width **bin_width** (km) from **rmin** to
**rmax**, and statistics (mean, median, standard deviation, min, max)
are computed for each radial bin, optionally as a function of longitude.

Output: a CSV table and optionally a radial profile GRASS raster of
ring brightness versus radius.

## EXAMPLES

Compute 10 km-wide radial brightness profile of Saturn's B ring:

```sh
p.rings.stats input=saturn_rings_projected \
    rmin=92000 rmax=117580 bin_width=10 \
    output=bring_profile.csv radial=bring_radial
```

## REFERENCES

- Hedman, M.M. & Nicholson, P.D. (2013). Kronoseismology: Using
  density waves in Saturn's C ring to probe the planet's interior.
  *Astronomical Journal* 146(1):12.
  doi:[10.1088/0004-6256/146/1/12](https://doi.org/10.1088/0004-6256/146/1/12)

## NOTES

The input raster must be in ring-plane coordinates (output of *p.rings.project*). The CSV output columns are: `r_km`, `mean`, `median`, `std`, `min`, `max` — plus `lon_deg` when longitude binning is enabled. The optional radial-profile raster is in the same units as the input map.

## SEE ALSO

*[p.rings.project](p.rings.project.md)*

## AUTHOR

Yann Chemin
