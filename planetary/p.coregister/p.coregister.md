## DESCRIPTION

*p.coregister* co-registers two raster maps by estimating and correcting
a translational pixel shift using **FFT-based phase correlation**
(Foroosh et al. 2002, IEEE Trans. Image Process. 11:188-200).

Both rasters must share the same GRASS location, mapset and resolution.
Use `g.region` to align them before running.

### Algorithm

1. Both rasters are loaded into memory and NULL cells replaced by band mean.
2. A 2-D Hann window is applied (unless `-w` is given) to reduce spectral
   leakage from non-periodic boundary conditions — recommended for real
   remote-sensing imagery.
3. Both windowed rasters are mean-subtracted and forward-FFT'd (FFTW3).
4. The normalised cross-power spectrum is formed:
   `C = S·conj(M) / |S·conj(M)|`
5. The inverse FFT of C is the **phase-correlation surface**; its peak at
   `(dy, dx)` is the translational shift of slave relative to master.
6. A 2-D parabolic fit around the peak gives sub-pixel precision.
7. Optionally (`-n`), a normalised cross-correlation (NCC) search over a
   `search=` pixel window refines the estimate — useful for low-contrast
   or noisy images.
8. The registered slave is written using bilinear interpolation.
9. A CSV shift report is written (or printed to stdout).

### Limitations

- **Translation only**: no rotation, scale or affine correction.
- Both rasters must cover the same extent and resolution (`g.region` first).
- Phase correlation is most accurate when both images have broadband
  spectral content (textured terrain, radar backscatter). For smooth
  or globally periodic images, disable the Hann window (`-w`) and use
  NCC refinement (`-n`).
- The maximum detectable shift is `rows/2` × `cols/2` pixels.

## PARAMETERS

| Parameter | Default | Description |
|---|---|---|
| `master=` | required | Reference raster |
| `slave=` | required | Raster to be registered |
| `output=` | required | Registered (shifted) slave output |
| `report=` | stdout | CSV shift report file |
| `search=` | 5 | NCC refinement search radius [pixels] (with `-n`) |
| `-n` | off | Refine with NCC (slower, more robust) |
| `-w` | off | Disable Hann window (use for DFT-aligned or noise-free signals) |

## OUTPUT

The shift report CSV contains one data row:
```
dx_pix, dy_pix, dx_m, dy_m, method
```

- `dx_pix`, `dy_pix`: shift in pixels (positive = slave is shifted east/south)
- `dx_m`, `dy_m`: shift in map units (ewres × dx_pix, nsres × dy_pix)
- `method`: `phase_correlation` or `ncc_refined`

## EXAMPLES

Co-register two CTX strips over the same region:

```sh
g.region raster=ctx_2012
p.coregister master=ctx_2012 slave=ctx_2015 output=ctx_2015_reg \
    report=ctx_shift.csv
```

Co-register CRISM with HiRISE (NCC refinement for noisy hyperspectral):

```sh
p.coregister master=hirise_red slave=crism_summary \
    output=crism_reg report=crism_shift.csv \
    search=10 flags=n
```

## NOTES

- Phase correlation requires FFTW3 (`libfftw3`).
- For sub-pixel NCC refinement, integer-pixel accuracy from phase
  correlation is used as the search centre; `search=` controls
  the ±radius of the NCC sweep (larger = more robust, slower).
- The registered slave always has NODATA margins at the shifted edges.

## REFERENCES

Foroosh H., Zerubia J.B. & Berthod M. (2002). Extension of phase
correlation to subpixel registration. *IEEE Trans. Image Process.*
11(3):188–200.

## SEE ALSO

*p.change*, *i.rectify*, *i.corr*
