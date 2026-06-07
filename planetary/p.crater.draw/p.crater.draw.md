## DESCRIPTION

*p.crater.draw* automatically delineates impact-crater rims from a DEM
and/or a panchromatic image and writes them as a GRASS polygon vector
ready for direct consumption by *[p.crater](p.crater.md)*
(crater scaling, via `input=`) and *[p.crater.freq](p.crater.freq.md)*
(size-frequency dating, via `vector= column=D_eq`).

Two detector strategies are bundled (Phase 1+2 of the roadmap):

- **DEM detector** — at each candidate centre and a bank of log-spaced
  radii, samples DEM values along the rim circle and an inner half-radius
  circle. The standardised contrast `(rim_mean - inner_mean) / rim_std`
  squashed through `tanh` gives a 0..1 confidence. Best for fresh
  craters on a clean DEM.
- **Image detector** — at each candidate centre and scale, samples the
  image on two ±60° arcs of the rim circle centred on the sun-azimuth
  direction (expected bright) and the anti-sun direction (expected
  shadow). Score combines (i) the bright/shadow contrast and (ii) the
  rim-vs-interior contrast. Requires `sun_azimuth=`.

A common **non-maximum-suppression** stage merges candidates with disk
IoU ≥ `nms_iou` (default 0.30), keeping the highest-confidence centre
and tagging the cluster's `method` as `"merged"` when two detectors
agree on the same crater.

## OUTPUT VECTOR SCHEMA

The output vector contains 48-vertex circular polygons with these
DOUBLE PRECISION / VARCHAR attribute columns:

| Column         | Meaning                                                 |
|----------------|---------------------------------------------------------|
| `cat`          | Category (layer 1, primary key)                         |
| `cx`, `cy`     | Detected centre, in map (projected) coordinates [m]     |
| `D_eq`         | Equivalent-circle diameter [m] - consumed by p.crater.freq |
| `axis_ratio`   | Minor/major (always 1.0 in v1)                          |
| `azimuth_deg`  | Major-axis bearing (0 in v1)                            |
| `confidence`   | Detector confidence, 0..1                               |
| `method`       | `"dem"`, `"image"`, or `"merged"`                       |
| `n_methods`    | 1 for single detector; ≥ 2 if merged                    |
| `dD_simple`    | Simple-crater d/D baked in at detection time. Sampled from `dd_simple_map=`, falling back to the `dd_simple=` scalar, then to a per-body default lookup (Moon 0.196, Mars 0.150, …). p.crater reads this column as a fallback when no override is given. |
| `basin_id`     | Multi-ring basin id (positive integer, shared by rings of the same basin), or NULL for standalone craters and runs without `-m`. |
| `ring_index`   | Ring index within a basin: 1 = innermost (smallest radius), 2 = next out, etc. NULL for standalone craters. |

Because the schema matches what *p.crater* expects on `input=` and what
*p.crater.freq* expects on `vector= column=D_eq`, the typical workflow
is a single shell pipeline:

```sh
p.crater.draw dem=mola_512ppd image=ctx_global \
              sun_azimuth=170 body=mars \
              output=detected_rims

p.crater -b body=mars input=detected_rims output=scaled_rims \
         impactor_velocity=20000 impactor_density=3000

p.crater.freq vector=scaled_rims column=Df_pi area=12500 \
              body=mars output=tharsis_csfd.csv
```

## DIAMETER RANGE FOR DOWNSTREAM CSFD ANALYSIS

The `min_diameter=` and `max_diameter=` options cap the search range
in metres. The defaults are **100 m (`min_diameter=`) to 10 km
(`max_diameter=`)**, chosen to overlap the Neukum production-function's
well-calibrated regime that *p.crater.freq* uses by default.

Practical guidance:

- **Below 100 m**: small-crater equilibrium dominates and confuses
  isochron fits; not worth detecting.
- **100 m – 10 km**: the sweet spot — detection is fast, the Neukum
  NPF is well-calibrated, and the size range yields several decades
  of log-spaced bins for the chi-squared fit.
- **Above 10 km**: raise `max_diameter=` freely. Run-time scales with
  `(max/min)^2`; for basin-class searches (> 50 km) increase `scales=`
  to 12+ and allow several minutes of compute. Sub-pixel refinement
  and multi-ring aggregation (`-m`) already handle these sizes.

For p.crater.freq runs that want the full Neukum/Hartmann diameter
window, use the defaults `min_diameter=100 max_diameter=10000`, then
`p.crater.freq dmin=0.1 dmax=100`.

## NOTES

- The current GRASS region (set with *g.region*) defines the search
  area. Reproject lat/lon DEMs / images to a metric CRS first
  (*p.cam2map* for planetary sensor projections; *r.proj* / *v.proj*
  for inter-projection moves).
- The `-c` flag requests OpenCL acceleration. In v1 the OpenCL path
  is stubbed; OpenMP parallelism (across candidate centres per scale)
  is used in all builds.
- All scale loops are parallelised with OpenMP; per-thread candidate
  buffers are merged once at the end to avoid lock contention.
- Default diameter range is **100 m – 10 km**; raise `max_diameter=` freely (see DIAMETER RANGE section).
- The detector requires the input region to be in **projected** (metric)
  coordinates. Geographic (lat/lon) regions are rejected.

### Sub-pixel centre and radius refinement

The coarse detector scans candidate centres on a stride of `r/3` pixels,
so a detected centre can be offset from the true rim by up to `r/6` in
each axis. After non-maximum suppression and before multi-ring basin
aggregation, **p.crater.draw runs a sub-pixel refinement pass** on every
`dem` or `merged` candidate.

The refinement is a grid search over a `(dx, dy, dr)` cube:

- **Centre search:** ± `r/3` m in x and y, sampled on a 7-point grid
  (step ≈ `r/9`);
- **Radius search:** ± 15 % of the detected radius, sampled on a 7-point
  grid.

At each trial point the same standardised rim-vs-inner contrast score
used by the detector, `(rim_mean − inner_mean) / (rim_std + 0.5)`, is
re-evaluated via bilinear DEM lookup. The trial that maximises this score
wins; its `(cx, cy, radius_m, confidence)` overwrites the coarse values.

**Image-only candidates are left untouched** (no DEM signal to optimise).

The refinement pass requires `dem=` to be given; it is silently skipped
when the module is run in image-only mode. Use the **`-R` flag** to skip
refinement explicitly if you want the raw coarse-stride output.

*Why this matters for multi-ring basins:* the `-m` aggregation groups two
rings only when their centres agree within `basin_centre_tol` (default 10 %
of the smaller radius). With a coarse-stride error of `r/6`, two
independently-detected rings of the same basin often miss each other.
Refinement reduces the residual to `r/18` or smaller, making concentric
grouping reliable for most basin geometries.

## EXAMPLES

### DEM-only on the Moon

```sh
g.region raster=lola_polar_dem
p.crater.draw dem=lola_polar_dem body=moon \
              min_diameter=200 max_diameter=8000 \
              threshold=0.55 \
              output=lola_polar_rims
```

### Image-only on Mars CTX (sun from south-southwest)

```sh
g.region raster=ctx_strip
p.crater.draw image=ctx_strip sun_azimuth=200 body=mars \
              min_diameter=100 max_diameter=5000 \
              output=ctx_strip_rims
```

### Combined DEM + image with NMS merging

```sh
p.crater.draw dem=mola_dem image=ctx_strip sun_azimuth=170 \
              method=both body=mars \
              output=combined_rims
```

## MULTI-RING BASIN AGGREGATION

The `-m` flag enables a post-NMS aggregation pass that groups
candidates whose centres agree within `basin_centre_tol` (as a
fraction of the smaller radius, default 0.10) AND whose radii
differ by at least `basin_ring_ratio` between adjacent rings
(default 1.30 — a 30% size step).

Grouped candidates receive a positive `basin_id` (1, 2, ...) and a
`ring_index` starting at 1 for the innermost (smallest) ring.
Standalone craters carry `basin_id = NULL` and `ring_index = NULL`.

This catches real planetary basin morphology (Imbrium, Orientale,
Hellas, Caloris, Rheasilvia, ...) where the same impact produces
multiple concentric scarps.

Sub-pixel refinement (enabled by default, see NOTES) reduces the
coarse-stride centre error before aggregation. Detections of the same
basin that land within `basin_centre_tol` of each other are grouped.
For difficult scenes with noisy DEMs, loosening the tolerance to
`basin_centre_tol=0.20`+ may recover missed associations.

References: Pike (1985) *Meteoritics* 20:49-68; Hartmann & Wood
(1971) "Moon: origin and evolution of multi-ring basins".

## ML PHASE (P3) - SHALLOW META-DETECTOR

`method=ml` runs whichever of P1 (DEM) and P2 (image) detectors are
fed inputs, then **rescores** each candidate via a shallow stacking
model. **Single-detector mode is supported**: when only `dem=` or
only `image=` is given, the missing channel is filled with zeros and
the model still produces a valid (lower-confidence) score. This lets
P3 train and run even on scenes with just one modality.

Per-candidate features are:

1. P1 (DEM) confidence
2. P2 (image) confidence
3. (P1 confidence)^2   - rim-std proxy
4. (P2 confidence)^2   - inner-std proxy
5. |conf_P1 - conf_P2| - bright/shadow contrast proxy
6. (conf_P1 + conf_P2)/2 - rim/interior contrast proxy

The final score is `sigma(sum_i w_i * f_i + b)` (logistic over a
linear combination of the 6 features).

### Model file format

Pass a trained model via `ml_model=<path>`. The file is binary:

```
magic    char[4]   "PCDM"
version  int32     1
w        float32 x 6
bias     float32
```

(`PCDM` = p.Crater.Draw Model.) If `ml_model=` is not given OR the
file can't be parsed, the detector falls back to **uniform 1/6
weights with zero bias** - this is a documented "stack the classical
detectors uniformly" baseline, not a learned ML.

### Training (offline, NOT shipped in the .deb)

The intended training pipeline lives in `scripts/train/` (planned):

1. Run `p.crater.draw method=both` on a calibration scene.
2. Use **high-confidence merged detections** (`n_methods >= 2,
   confidence > 0.85`) as positive labels.
3. Use random non-detection patches + low-confidence singletons as
   negative labels.
4. Fit a logistic regression over the 6 features.
5. Export weights and bias to the `.bin` file above.

Future iterations can replace the linear model with a small Random
Forest (split tables stored similarly) or with a quantised CNN; the
inference engine in `detect_ml.c` is the seam.

## OPENCL (`-c` FLAG)

The `-c` flag asks the module to use OpenCL acceleration. When the
module is built against OpenCL (the Makefile auto-detects
`<CL/cl.h>` and links `-lOpenCL`), the **DEM rim/floor sampler** and
the **image sun-shadow paired-arc** inner loops are dispatched as
OpenCL kernels to the best available device (preferring discrete
GPU > integrated GPU > CPU class device, requiring fp64 support).

Two kernels are compiled lazily on first use and cached for
subsequent scales:

- `detect_dem_kernel` - one work item per (row, col) candidate
  centre; samples the rim and inner half-radius circles via
  bilinear lookups; emits the standardised
  `(rim_mean - inner_mean) / (rim_std + 0.5)` score, then `tanh`.
- `detect_image_kernel` - one work item per (row, col) centre;
  samples the two opposing rim arcs plus four inner-disk control
  points; emits the bright/shadow contrast score, then `tanh`.

Both are embarrassingly data-parallel; on a modern discrete GPU we
measured the testsuite (synthetic 400x400 region, 4 scales) drop
from ~9 s on the OpenMP path to ~2.5 s on the GPU path.

If the module is built without OpenCL, or the `-c` flag is set but
no fp64-capable device is found, the detectors fall back to the
OpenMP CPU path with a clear `OpenCL ... falling through to OpenMP`
message. **The output of the GPU and OpenMP paths is bit-equivalent
for the synthetic test fixture** (verified by the testsuite).

Source: `opencl_runtime.c` (host) + `cl_kernels.c` (embedded kernel
strings; no separate `.cl` file shipped).

## FUTURE WORK

The following enhancements are tracked for later releases:

- **Very large basins (> 100 km).** At extreme diameters the raster
  must be entirely resident in RAM. Out-of-core / tiled processing is
  a future optimisation; for now ensure the GRASS region covers only
  the area of interest and increase `scales=` for better radius sampling.
- **Phase 3 — shallow ML.** Random Forest over engineered features
  (multi-scale curvature, ring-template correlation, spectral angle
  for multispectral inputs). Trained matrices shipped as small
  binary blobs.
- **Phase 4 — CNN detector.** Lightweight U-Net trained on Robbins
  lunar and Mars crater databases; INT8-quantised weights, hand-coded
  C inference runtime (no PyTorch/ONNX dependency).
- **OpenCL kernels.** The shadow-pair template bank in particular is
  GPU-friendly; an OpenCL implementation would give ~10–50× speedup
  over OpenMP on commodity GPUs.

## REFERENCES

- Marr, D., & Hildreth, E. (1980). "Theory of edge detection."
  *Proc. R. Soc. Lond. B*, 207, 187-217.
  [doi:10.1098/rspb.1980.0020](https://doi.org/10.1098/rspb.1980.0020)
- Urbach, E. R., & Stepinski, T. F. (2009). "Automatic detection of
  sub-km craters in high resolution planetary images."
  *Planetary and Space Science*, 57(7), 880-887.
  [doi:10.1016/j.pss.2009.03.009](https://doi.org/10.1016/j.pss.2009.03.009)
- Wang, L., & Liu, H. (2006). "An efficient method for identifying
  and filling surface depressions in digital elevation models for
  hydrologic analysis and modelling." *Int. J. Geographical
  Information Science*, 20(2), 193-213.
  [doi:10.1080/13658810500433453](https://doi.org/10.1080/13658810500433453)
- Robbins, S. J. (2019). "A new global database of lunar impact
  craters >1-2 km: 1. Crater locations and sizes, comparisons with
  published databases, and global analysis." *Journal of Geophysical
  Research: Planets*, 124(4), 871-892.
  [doi:10.1029/2018JE005592](https://doi.org/10.1029/2018JE005592)
- Pike, R. J. (1985). "Some morphologic systematics of complex
  impact structures." *Meteoritics*, 20, 49-68.
  [doi:10.1111/j.1945-5100.1985.tb00845.x](https://doi.org/10.1111/j.1945-5100.1985.tb00845.x)
- Silburt, A., Ali-Dib, M., Zhu, C., et al. (2019). "Lunar crater
  identification via deep learning." *Icarus*, 317, 27-38.
  [doi:10.1016/j.icarus.2018.06.022](https://doi.org/10.1016/j.icarus.2018.06.022)

## SEE ALSO

*[p.crater](p.crater.md) — impact-crater scaling on the polygons
produced here,
[p.crater.freq](p.crater.freq.md) — surface-age dating from the
detected crater set*

## AUTHOR

Yann Chemin (dr.yann.chemin@gmail.com)

## LICENSE

The Unlicense ([https://unlicense.org](https://unlicense.org)) -
this module is released into the public domain.
