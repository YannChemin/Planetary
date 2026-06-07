## DESCRIPTION

*p.desmear* corrects image smear caused by sensor readout during
spacecraft motion. In push-broom or TDI (time-delay integration)
cameras, finite readout time combined with spacecraft motion causes
each pixel to accumulate signal over a range of ground positions,
producing a motion blur (smear) in the along-track direction.

The correction models smear as a convolution with a rectangular
(boxcar) kernel of length **smear_lines** in the along-track direction
and deconvolves using a recursive filter. The smear length in pixels
depends on the frame rate, line time, and spacecraft velocity.

## NOTES

Desmearing is most critical for MESSENGER MDIS (frame camera) and
HiRISE (TDI line scanner) products near spacecraft manoeuvres.

## EXAMPLES

Desmear a MESSENGER MDIS frame image (smear length ≈ 3 pixels):

```sh
p.desmear input=mdis_nac_raw smear_lines=3 direction=line \
    output=mdis_nac_desmeared
```

## REFERENCES

- Bannister, R.A. et al. (2016). Desmearing of TDI imager data.
  *Proc. SPIE* 9977:997713.
  doi:[10.1117/12.2236952](https://doi.org/10.1117/12.2236952)

## SEE ALSO

*[p.dstripe](p.dstripe.md),
[p.cubenorm](p.cubenorm.md)*

## AUTHOR

Yann Chemin
