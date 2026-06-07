# Mission configuration files

Each `*.json` file in this directory describes one landing mission: where it
landed (or is planned to land), the lander's safety requirements, and the
analysis parameters `p.landing` (and the individual `p.*` modules) use to
reproduce the site evaluation. The body radii/projection come from a separate
descriptor in `../bodies/` (`moon.json`, `mars.json`, `venus.json`), passed via
`body=`.

Pass a mission file to the pipeline with:

```sh
p.landing dem=<raster> body=../bodies/moon.json mission=missions/<name>.json ...
```

## Schema

All fields are optional unless marked **required**. Unknown fields are ignored,
so the files are forward-compatible.

### Identity

| Field | Type | Description |
|---|---|---|
| `mission` | string | **required** — human-readable mission name |
| `body` | string | **required** — body name; must match a `bodies/<name>.json` `name` |
| `lander_type` | string | free-text class, e.g. `crewed_lander`, `robotic_rover` |
| `notes` | string | provenance / caveats |

### `landing_site` (documentary)

Records the actual or planned touchdown point. **Documentary only** — the
current pipeline does *not* derive the computational region from it; set the
region from your DEM (or via `region_bounds`, below). It exists so a mission can
be tied to a real, citable location.

| Key | Type | Description |
|---|---|---|
| `name` | string | place name (e.g. "Jezero crater") |
| `lat_deg` | number | planetocentric latitude, degrees (north positive, −90..90) |
| `lon_deg` | number | longitude, degrees |
| `lon_convention` | string | how `lon_deg` is expressed; the bundled files use `east_positive_0_360_planetocentric` |
| `epoch_utc` | string | landing date/time, ISO-8601 UTC (e.g. `2021-02-18T20:55:00`) |
| `status` | string | outcome, e.g. `landed_success`, `landed_tipped`, `planned`, `crashed` |

Coordinates in the bundled files are approximate published landing-site values.
To analyse a site you still need a DEM that covers it.

### Region (optional)

| Field | Type | Description |
|---|---|---|
| `region_bounds` | object | `{n,s,e,w,res}` in the **Location's projected CRS** (metres for projected, degrees for longlat). Applied with `g.region` and clipped to the DEM extent. Use for a fixed reproducible window (see `luna27.json`, `luna27_article.json`). |
| `study_area_note` | string | printed at run start and stored in the report; explains any divergence from the published site (e.g. DEM coverage limits) |

If `region_bounds` is absent the active region is used as-is.

### Lander footprint & ranking

| Field | Type | Description |
|---|---|---|
| `ellipse_major_m` | number | landing-ellipse full major axis (m); drives `p.rank` minimum candidate area |
| `ellipse_minor_m` | number | landing-ellipse full minor axis (m) |
| `top_percentile` | number | suitability percentile threshold for candidate extraction (e.g. `70`) |

### Analysis parameters

| Field | Type | Description |
|---|---|---|
| `scan_res` | number | working resolution (m) for slope/illumination/visibility |
| `ephemeris` | string | sub-solar/sub-Earth model: `auto` (default), `spice`, `meeus`, `analytic` — cascade `spice → meeus → analytic` |
| `start_epoch` | string | ISO-8601 UTC start epoch for the real ephemeris (Meeus/SPICE) |
| `illum_nsteps` | integer | timesteps for `p.illumination.sunfraction` over the cycle |
| `vis_nsteps` | integer | timesteps for `p.visibility.earth` |
| `earth_elevation_min_deg` | number | minimum Earth elevation above the local horizon to count as visible |
| `orbiter_altitude_km` / `orbiter_inclination_deg` | number | relay-orbiter geometry for `p.visibility.orbiter` |

### Thresholds, weights, exclusions

| Field | Type | Description |
|---|---|---|
| `slope_thresholds_deg` | object | `{scale_m: max_slope_deg}` per analysis scale (e.g. `{"30":10,"100":7}`) |
| `roughness_rms_max_m` | number | RMS-height roughness limit |
| `min_illumination_fraction` | number | minimum acceptable illumination fraction [0..1] |
| `earth_visibility_min_fraction` | number | minimum acceptable Earth-visibility fraction [0..1] |
| `science_targets` | array | tags, e.g. `["volatile_ice","geology"]` |
| `criteria_weights` | object | MCDM weights: `slope, roughness, illumination, earth_vis, science` (should sum to ~1) |
| `hard_exclusion` | object | hard cut-offs: `slope_max_deg, roughness_rms_max_m, illumination_min, earth_vis_min` |

## Body applicability notes

- **Far-side Moon** missions (`change4`, `change6`) have no direct Earth
  visibility; their `earth_vis` weight is `0` (relay-dependent).
- **Venus** missions (`venera13`, `venera14`, `vega2`) are documentary: the
  opaque atmosphere makes surface solar-illumination and Earth-visibility
  analysis inapplicable; only radar-derived terrain (Magellan) is meaningful.
  Their illumination/earth_vis weights are `0`.

## Bundled missions

| File | Body | Site |
|---|---|---|
| `luna27.json` | Moon | Luna-27 polar cap baseline (LOLA 5 m, 87.5–90°S) |
| `luna27_article.json` | Moon | Turchinskaya & Slyuta 2024 sector (79–83°S; needs `ldem_75s_30m`) |
| `artemis.json` | Moon | Artemis south-polar (generic, high illumination requirement) |
| `apollo11/15/17`, `luna9`, `luna17_lunokhod1`, `change4`, `change6`, `chandrayaan3`, `slim`, `im1_odysseus` | Moon | historic/recent lunar landings |
| `viking1/2`, `pathfinder`, `spirit`, `opportunity`, `phoenix`, `curiosity`, `insight`, `perseverance`, `zhurong` | Mars | historic/recent Mars landings |
| `venera13/14`, `vega2` | Venus | Soviet Venus landers (documentary) |
