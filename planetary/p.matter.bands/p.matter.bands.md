# p.matter.bands — Planetary matter detection from absorption bands

## DESCRIPTION

**p.matter.bands** detects and maps surface/atmospheric matter (minerals,
ices, gases, organics, liquids) in any planetary hyperspectral or multispectral
image group by computing absorption-band depth maps from a science-backed band
database covering UV through LWIR (0.1–100 µm).

The module is sensor-agnostic and body-aware: given a target body
(`body=`), it queries the built-in band database
(`$GISBASE/etc/planetary/matter_bands.json`) for all species detectable on
that body, filters to those whose diagnostic absorption bands fall within the
sensor's wavelength coverage, and computes one per-species band-depth raster.

### Key concepts

**Absorption band depth** — following Clark & Roush (1984):

```
BD = 1 - R_center / R_continuum
```

where `R_continuum` is the linear continuum interpolated from the left and
right shoulders.  A value of 0 means no absorption; 1 means total absorption.
The continuum is removed first (following Sunshine et al. 1990, `p_spectra`
library) before depth computation, making results comparable across sensors
and illumination conditions.

**Body-aware filtering** — the band database encodes which matter types are
plausible on each planetary body based on geochemical constraints and published
detection. For example, NH4-bearing phyllosilicates are listed only for Ceres
and the outer C-type asteroids, not for the Moon.

**Sensor-coverage gating** — each species entry carries `detection_range_um`
(min/max wavelength needed to detect it). If the image group does not cover
this range, the species is silently skipped and listed in the coverage-gap
report.

---

## SITUATION REPORT — EXISTING SPECTRAL MODULES

The following spectral/mineral analysis modules already exist in the addons.
`p.matter.bands` extends this stack to broader wavelength coverage and all
planetary body types.

| Module | Role | Body / sensor scope | Wavelength |
|---|---|---|---|
| `p.mineral.indices` | CRISM-derived Mars indices (OLINDEX, LCPINDEX, HCPINDEX, D2300, BD1900, BD3000) | Mars / CRISM, OMEGA | 1.0–3.5 µm |
| `p.spectral.planet` | Generic band-ratio, NDI, SAM, band-depth, continuum-removal operations | Any / any multispectral | User-defined |
| `p.bandnorm` | Spectral band normalisation (illumination removal) | Any | Any |
| `p_spectra` (lib) | Core C library: band depth, SAM, continuum-removal, high-pass, divfilter | (library) | (library) |
| `p.photomet` | Photometric correction (Lambert–Hapke models) | Any rocky/icy body | (broadband) |
| `p.atcorr.hapke` | Atmospheric correction via Hapke RT | Atmosphered bodies | (broadband) |
| `p.albedo` | I/F → geometric/normal albedo conversion | Any | (broadband) |
| `p.phocube` | Per-pixel geometry backplanes (i, e, g angles) | Any | (broadband) |
| `p.cubenorm` / `p.dstripe` | Detector gain/stripe removal (pre-processing) | Any push-broom | Any |

**Gap addressed by `p.matter.bands`**:
- `p.mineral.indices` is Mars-only and CRISM-centric (6 minerals only).
- No module addresses ices, gases, organics, or liquids.
- No module covers UV (<0.36 µm) or LWIR (>5 µm).
- No module provides a body-aware filter (prevents physically unreasonable
  detections, e.g. reporting serpentine on Io or SO2 ice on Mercury).
- No spectral database exists in the repo; all band parameters are hard-coded.

---

## PLANETARY BODY APPLICABILITY

### Band detection ranges by body type

```
Body class       UV(0.1-0.4) VIS(0.4-0.7) NIR(0.7-1µm) SWIR(1-2.5) MIR(2.5-5) TIR(5-25µm) FIR(>25µm)
──────────────── ─────────── ──────────── ─────────── ──────────── ────────── ─────────── ──────────
Rocky/basaltic   SO2,graphit Fe-oxides    olivine,Px   phyllosil.  carbonate  silicate-8  carbonate-11
 (Mars,Moon,Merc          SO2,s-allot  feldsp.glass  sulfates    organics   emiss.      emiss.
  Venus surface)                      TiO2/FeO      ices(ov.t.)  H2O ice
Ice worlds       SO2(Io)     S-allot(Io) H2O-ice(ov) H2O,CO2,N2  H2O,CO2,   SO2 fund.  H2O fund.
 (Europa,Encela              (UV broad)   CH4,SO2-1st CH4,CO,NH3  SO2,CH4    H2O2,CO2
  Io,Titan,Pluto)                         band         ice        fund.
Gas/ice giants   UV hazes    CH4 atm     CH4,NH3      CH4,NH3    NH3,H2S    NH3 fund.  H2 CIA
 (Jupiter,Saturn             H2 Rayleigh  rings-min.  PH3,GeH4   PH3,AsH3   CH4 fund.
  Uranus,Neptune)
Moons (rocky)    space-weath Fe-oxides    olivine,Px   phyllosil. carbonate  silicate-8  emiss.
 (like Moon)     SO2(Io exc)              glass,TiO2   ices(ov.t.) ice fund.
Asteroids (C)    UV slope    0.7µm drop  olivine,Px   hydrated   3µm OH     emiss.
                             Fe/Fe-OH                 silicate   organics
Asteroids (S)    UV          Fe-oxides   olivine(1µm)  Px(1.85µm) —
Comets                       dark org.   org+ice-ov.  ice combs  organics   H2O,CO2    H2O rot.
                                                                  C-H 3.4
KBOs / TNOs                  tholins     CH4,N2,CO-ov CH4,N2,CO  —          CH4 fund.
Exoplanet atm   UV,ozone    aerosols    CH4,H2O      CO2,H2O    CO2,CH4    CO2 bending
 (transmission)             hazes        absorption   CO,HCN     CH4 fund.
```

### Body-specific matter matrix

#### Mars — well-studied; CRISM/OMEGA data-rich
- **Minerals**: olivine (Mg-rich Noachian), low/high-Ca pyroxene (volcanic),
  phyllosilicates (smectite, kaolinite, serpentine, chlorite — Noachian/Hesperian),
  sulfates (jarosite, gypsum, kieserite — Hesperian/Amazonian),
  Fe-oxides (hematite, goethite), carbonates (minor), perchlorate hydrates
- **Ice**: H2O (polar caps, subsurface), CO2 (south polar cap seasonal)
- **Atm gas**: CO2 (dominant), H2O vapour, CO, dust
- **Key reviews**: Ehlmann & Edwards 2014 (Annu. Rev. Earth Planet. Sci. 42:291-315),
  Murchie et al. 2009 (JGR 114:E00D06)

#### Moon — space-weathered mafics; polar ice
- **Minerals**: anorthosite/plagioclase (highlands), orthopyroxene (crust),
  clinopyroxene (maria), volcanic glass (pyroclastics), ilmenite (high-Ti maria)
- **Ice**: H2O/OH at poles (PSRs); surface OH hydroxyl layer detected by M3
- **Refs**: Pieters et al. 2009 (Science 326:568), Mustard et al. 2011 (Nature 478:85)

#### Mercury — low-FeO, sulfur-rich, graphite-bearing
- **Minerals**: graphite (dark material), enstatite (low FeO), CaS/MgS (hypothesized),
  volcanic glass; **no hydration** detected
- **Sensor challenge**: low band contrast due to negligible FeO;
  BepiColombo/SIMBIO-SYS will provide improved spectral coverage
- **Refs**: Nittler et al. 2011 (Science 333:1847), Murchie et al. 2015 (Science 353:1397)

#### Venus surface — NIR atmospheric windows only
- **Surface**: basalt vs. tesserae emissivity contrast; pyroxene/feldspar
  discrimination through 1.0, 1.18, 2.3 µm atmospheric windows
- **Atmosphere**: SO2, CO2, HCl, H2SO4 cloud droplets
- **Future**: EnVision/VenSpec-M (2031) will improve surface sensitivity
- **Refs**: Hashimoto & Sugita 2003 (JGR 108:5109), Smrekar et al. 2010 (Science 328:605)

#### Titan — organic-dominated; hydrocarbon lakes; H2O bedrock
- **Organics**: tholins (haze, UV-VIS), aliphatic C-H (3.4 µm), aromatic C-H
- **Liquids**: methane/ethane lakes (specular at 2.0 µm in VIMS windows)
- **Ices**: H2O bedrock (rare exposure), CO2?
- **Atm**: N2+CH4+HCN; opaque in most of VNIR → use VIMS atmospheric windows
- **Future**: Dragonfly (2034) GC-MS + cameras in situ
- **Refs**: Stofan et al. 2007 (Nature 445:61), Brown et al. 2008 (Nature 454:607)

#### Europa — irradiated icy ocean world
- **Ices**: H2O crystalline vs. amorphous (irradiation signature in band shape),
  hydrated H2SO4, MgSO4·7H2O (epsomite), NaCl (colour centres)
- **Irradiation products**: H2O2 (3.5 µm), SO2 (4.08 µm), CO2 (4.26 µm)
- **Future**: Europa Clipper/MISE (2030) hyperspectral mapping at 25 m/pixel
- **Refs**: McCord et al. 1998 (Science 280:1242), Trumbo et al. 2019 (Sci. Adv. 5)

#### Io — SO2-dominated; sulfur allotropes; active volcanism
- **Surface**: SO2 frost (dominant; 4.08 µm), sulfur allotropes S8/Sn (VIS colour),
  active lava (thermal emission in MIR)
- **Atmosphere**: SO2 gas (UV, IR)
- **Refs**: Smythe et al. 1995 (Icarus), Douté et al. 2004 (Icarus 171:552)

#### Enceladus — clean H2O ice; plume organics
- **Ices**: near-pure H2O crystalline ice (very strong absorption bands)
- **Plume**: H2O, CO2, CH4, H2, silica nanoparticles, complex organics (3.4 µm)
- **Refs**: Brown et al. 2006 (Science 311:1425), Postberg et al. 2018 (Nature 558:564)

#### Ceres — ammoniated phyllosilicates; carbonates; water ice
- **Unique marker**: NH4+ absorption at 3.06 µm (phyllosilicate N-H stretch)
- **Minerals**: Mg-carbonate, Ca-carbonate, Na-carbonate (Occator faculae)
- **Ice**: H2O at Oxo crater (transient surface exposure)
- **Refs**: De Sanctis et al. 2015 (Nature 528:241), Carrozzo et al. 2018 (Sci. Adv.)

#### C-type asteroids (Bennu, Ryugu, Pallas…)
- **Hydration**: 0.7 µm Fe2+/Fe3+ drop; 2.72/2.94 µm O-H stretch
- **Organics**: 3.4 µm C-H (confirmed in Ryugu samples; Yabuta et al. 2023, Science)
- **Refs**: Kitazato et al. 2019 (Science 364:272), Hamilton et al. 2019 (Nature Astron.)

#### S-type asteroids (Itokawa, Eros…)
- **Silicates**: olivine 1.0/1.25 µm + pyroxene 0.9/1.85 µm; space-weathering
  reduces and reddens band depth (NPFe nanophase iron)
- **Refs**: Noguchi et al. 2011 (Science 333:1121), Binzel et al. 2002 (Icarus 151:139)

#### Comets (67P, Halley, future dynamically new comets)
- **Nucleus surface**: organic-rich dark material (broad 3.2 µm feature; Capaccioni et al. 2015)
- **Coma**: H2O, CO2, CO, OCS, CH4, HCN (fluorescence emission lines + absorption)
- **Refs**: Capaccioni et al. 2015 (Science 347:aaa0628), Quirico et al. 2016 (Icarus 272:32)

#### Pluto / KBOs — volatile ices + tholins
- **Ices**: CH4 (dominant; 1.67, 2.32 µm), N2 (2.15 µm), CO (1.58, 2.35 µm),
  CO2 (trace), H2O (dark terrain)
- **Tholins**: VIS-UV reddening slope; no sharp absorption
- **Refs**: Grundy et al. 2016 (Science 351:aad9189), Protopapa et al. 2017 (Icarus 287)

---

## MODULE DESIGN: p.matter.bands

### Parameters

```
p.matter.bands group=<name> body=<name> output_prefix=<name>
    [matter=<types>] [db=<path>] [min_bd=<0.0-1.0>]
    [-l] [-c] [-v]
```

| Parameter | Type | Description |
|---|---|---|
| `group` | required | GRASS image group with wavelength metadata (set by p.in.isis / p.in.pds3 / p.in.pds4) |
| `body` | required | Planetary body: `mars`, `moon`, `mercury`, `venus`, `titan`, `europa`, `io`, `enceladus`, `ceres`, `asteroid_c_type`, `asteroid_s_type`, `comet`, `pluto`, `generic` |
| `output_prefix` | required | Prefix for output raster maps (one per detected species) |
| `matter` | optional | Comma-separated subset: `minerals`, `ices`, `gases`, `organics`, `liquids`. Default: `all` |
| `db` | optional | Path to custom band database JSON (overrides built-in `matter_bands.json`) |
| `min_bd` | optional | Minimum band depth to report (0.0–1.0, default 0.01). Pixels below this threshold are set to NULL. |
| `-l` | flag | List detectable species for the body/sensor combination; do not process rasters |
| `-c` | flag | Output composite false-colour RGB: R=mineral, G=ice, B=organic (strongest species per type) |
| `-v` | flag | Verbose: print band depth statistics per species |

### Output maps

For each detectable species:

```
<output_prefix>_<species_name>   DCELL raster, band depth [0.0, 1.0]
                                 NULL where image data is invalid or BD < min_bd
```

If `-c`: `<output_prefix>_composite_RGB` (three-band group: R/G/B)

### Processing chain

```
                ┌──────────────────────────────────────────────────────┐
                │  p.matter.bands                                       │
                │                                                       │
  GRASS image   │  1. Read wavelength metadata from group              │
  group         │     (p_spectra_def from planetary.json band history) │
  (calibrated   │                                                       │
   I/F rasters) │  2. Load matter_bands.json for target body          │
        │       │                                                       │
        ▼       │  3. Gate: filter species to those within sensor       │
  band_list[]   │     wavelength range (detection_range_um)            │
                │                                                       │
                │  4. For each qualifying species:                      │
                │     a. p_spectra_continuum_remove(left, right)        │
                │     b. p_spectra_band_depth(left, center, right)      │
                │     c. Multi-band composite score if species has      │
                │        multiple absorption features                    │
                │     d. Write output raster                            │
                │                                                       │
                │  5. Coverage-gap report: species skipped + reason    │
                │                                                       │
                │  6. Optional: composite RGB (-c flag)                │
                └──────────────────────────────────────────────────────┘
```

### Band-depth computation (per species)

For species with N absorption bands b₁…bₙ:

```python
BD_total = 0
weight_sum = 0
for each band bᵢ:
    if sensor covers bᵢ.center ± tolerance:
        BD_i = band_depth(R[left_i], R[center_i], R[right_i])
        w_i  = diagnostic_weight[bᵢ]   # from database; primary feature = 1.0
        BD_total  += BD_i * w_i
        weight_sum += w_i
BD_species = BD_total / weight_sum if weight_sum > 0 else NULL
```

Multi-band confirmation raises confidence; single-band detections are flagged
with lower confidence.

---

## IMPLEMENTATION PLAN

### Phase 1 — Foundation (this PR)

**1.1 Band database** (`data/matter_bands.json`)
- Curated JSON: bodies × matter-types × absorption bands × references
- Fields: `name`, `formula`, `absorption_bands[]` (center/left/right/type),
  `detection_range_um`, `sensors[]`, `refs[]` (cite + DOI)
- Initial coverage: Mars (full), Moon, Mercury, Europa, Titan, Io, Enceladus,
  Ceres, asteroid C/S type, comet, Pluto
- Schema versioned (`_schema: matter_bands_v1`) for forward compatibility

**1.2 C library extension** (`libs/p_spectra/`)
- Add `p_spectra_matter_db_load()` — parse `matter_bands.json` into C struct
- Add `p_spectra_matter_db_filter()` — filter by body + wavelength coverage
- Add `p_spectra_bd_multi()` — weighted multi-band depth (handles partial coverage)
- Extend `PSpectraDef` to carry body + species metadata from JSON

**1.3 Module skeleton** (`planetary/p.matter.bands/`)
- Python wrapper calling the C library via GRASS ctypes interface
- Reads image group → builds per-pixel spectra → calls p_spectra band depth
- OpenMP-parallelized row processing (inherit from existing p_spectra pattern)

### Phase 2 — Wavelength range extension ✓ COMPLETE

**2.1 UV support (0.1–0.4 µm)** ✓
- 15 UV absorption entries across 8 bodies
- Mars: Fe-oxide O→Fe(III) charge-transfer at 0.22 µm (hematite, goethite)
- Mercury: graphite broad UV at 0.20 µm (MERTIS)
- Venus: SO2 at 0.28 µm + unknown UV absorber at 0.36 µm (EnVision/VenSpec-U)
- Titan: tholins 0.28 µm + benzene 0.21 µm (Cassini/UVIS, JWST)
- Io: SO2 frost 0.28 µm + S2 disulfur 0.38 µm + polysulfur 0.50 µm
- Comets: OH 0.308 µm, CN 0.388 µm, CS 0.258 µm
- Pluto: tholins 0.35 µm + CH4 UV 0.30 µm
- C-asteroids: magnetite UV slope 0.35 µm (Ryugu/Bennu)
- Sensors: MAVEN/IUVS, HST/STIS, EnVision/VenSpec-U, Cassini/UVIS, Comet Interceptor

**2.2 MIR support (5–30 µm)** ✓
- 38 MIR band features across all rocky bodies and icy moons
- `mode=emissivity` parameter added to module; BD formula is structurally identical
  (`BD = 1 − ε(λ)/ε_continuum`) — tagged per-species via `"mode": "emissivity"` in JSON
- Mars: olivine/pyroxene/plagioclase/carbonate/volcanic glass reststrahlen (TES/THEMIS);
  silicate dust opacity at 9.3 + 18 µm; CO2 9.4 µm combination band
- Moon: anorthosite 9.7 µm, mare pyroxene 9.3 µm, Mg-spinel 13.5 µm (Diviner)
- Mercury: enstatite pyroxene 9.1 µm + sulfide/oldhamite 10.5 µm (MERTIS)
- Io: SO2 frost TIR at 7.3/8.7/19.4 µm (JIRAM, JWST/MIRI)
- Venus: anhydrite (CaSO4) 8.7/14.2 µm (EnVision/MERTIS-V)
- Europa: H2O ice libration 6.1 µm + lattice phonon 12 µm (JWST/MIRI, Europa Clipper)
- Titan: benzene C6H6 14.85 µm + HCN 14.05 µm bending (Cassini/CIRS, JWST)
- Enceladus: NH3 umbrella 9.0 µm (JWST/MIRI)

**2.3 LWIR/FIR (30–100 µm)** ✓
- Comet H2O coma FIR rotational lines: 56.9 µm and 179.5 µm (Herschel/PACS, HIFI)
- Mars CO2 15 µm, water_vapor 25 µm stubs extended (existing entries)
- Sensors: Herschel/PACS, Herschel/HIFI, ALMA

### Phase 3 — Body database expansion ✓ COMPLETE

**New bodies added (6):**

| Body | Key species | Driver mission |
|---|---|---|
| `ganymede` | H2O ice, CO2 ice, MgSO4 hydrate salt, SO2 trace (UV), dark organics | JUICE/MAJIS 2031 |
| `callisto` | H2O ice, CO2 ice (dominant), dark C-rich lag | JUICE/MAJIS 2031 |
| `triton` | N2 ice (2.148 µm), CH4 ice (6 bands), CO ice, CO2 ice, H2O ice, tholins | future orbiter (Decadal) |
| `ariel` | CO2 ice (very strong, trailing), H2O ice, NH3 hydrate, dark material | Uranus Orbiter (Decadal) |
| `uranus_moon` | H2O ice, CO2 ice, NH3 hydrate, dark organics (Titania/Oberon/Umbriel) | Uranus Orbiter (Decadal) |
| `asteroid_d_type` | Organic reddening slope, aliphatic + aromatic C-H, OH phyllosilicates | Lucy ongoing |

**Expanded existing bodies:**
- **Europa** (+3 minerals): hydrous silica SiO2·nH2O (Hand & Carlson 2007), NaHCO3 (Trumbo et al. 2022 JWST), FeCl2·4H2O iron chloride brine (McCord et al. 2010)
- **Titan** (+3 ices): HCN ice (Dragonfly target, 1.50/3.02/4.76 µm), HC3N cyanoacetylene (3.29/15.08 µm), benzene ice polar deposits (1.685/3.22 µm)
- **Venus** (+3 gases): HDO semi-heavy water D/H tracer (1.38/2.55/3.67 µm), H2SO4 aerosol cloud droplets (2.48/3.70/9.0 µm), CO lower atmosphere (2.33/4.67 µm)

**Database totals after Phase 3:** 19 bodies, 123 species, wavelength range 0.18–200 µm

### Phase 5 — Confidence and quality outputs ✓ COMPLETE

**5.1 Per-species confidence raster (`-q`)**
- Flag `-q`: writes `<prefix>_<species>_conf` alongside each BD map
- Value in [0, 1] = n_diagnostic_bands_matched / n_diagnostic_bands_total
- Grey colour table; NULLed where BD is NULL (< min_bd)
- Tells users immediately which detections are single-band guesses vs. fully confirmed

**5.2 Minimum confidence filter (`min_conf=`)**
- `min_conf=` (default 0.0 = disabled): suppresses species whose confidence falls below threshold
- Suppressed species are logged and appear in the JSON report's `skipped` list
- Example: `min_conf=0.667` requires at least 2 of 3 diagnostic bands in sensor

**5.3 JSON detection report (`report=`)**
- `report=<path>`: writes a structured JSON summary after all species are processed
- Top-level fields: `body`, `mode`, `sensor_min_um`, `sensor_max_um`, `n_bands`,
  `in_range`, `out_of_range`, `n_detections`, `n_skipped`, `detections[]`, `skipped[]`
- Per-detection fields: `name`, `mtype`, `n_diagnostic_bands`, `n_matched`,
  `confidence`, `n_valid_pixels`, `mean_bd`, `max_bd`, `output_map`, `note`
- Machine-readable for downstream pipelines and science workflows

**5.4 Confidence-weighted composite (`-c`)**
- Composite RGB channel selects the "best" species per type by `confidence × mean_BD`
  rather than bare `mean_BD`
- Prevents a low-confidence single-band detection from dominating the composite
  over a fully confirmed multi-band detection with slightly lower BD

### Phase 6 — Classification and uncertainty propagation ✓ COMPLETE

**6.1 Dominant-species classification map (`-k`)**
- Flag `-k`: writes `<output_prefix>_classification`, a CELL raster where each
  pixel holds the category code of the species with the highest
  `confidence × BD` score at that location
- `r.category` labels attach each code to its species name; `r.colors color=random`
  gives a distinguishable per-category colour table
- NULL where no species cleared `min_bd`/`min_conf` at that pixel
- Useful as a single browse product analogous to CRISM/OMEGA mineral-class maps

**6.2 Radiometric uncertainty propagation (`-e`, `radiometric_noise=`)**
- `radiometric_noise=` (default 0.0 = disabled): fractional 1-sigma uncertainty
  assumed on every input reflectance/emissivity sample
- Propagated analytically through the linear-continuum band-depth formula
  (first-order error propagation on `BD = 1 − R_c/R_cont`); per-feature sigmas
  combined with the same diagnostic-band weights used for the BD itself
- Flag `-e` writes `<prefix>_<species>_unc` (1-sigma BD uncertainty, grey
  colour table); without `radiometric_noise=` set, `-e` has no effect (warns)
- Scales with any space-weathering correction applied (same linear factor)
- JSON report gains a `mean_uncertainty` field per detection (`null` when disabled)

### Phase 7 — Multi-temporal change detection ✓ COMPLETE

**7.1 Band-depth difference map (`-d`, `reference_prefix=`)**
- `reference_prefix=`: the `output_prefix=` from a previous run over the same
  species/body/sensor combination (e.g. an earlier observation epoch)
- Flag `-d`: for each detected species, looks up `<reference_prefix>_<species>`
  and writes `<output_prefix>_<species>_diff = BD_now − BD_reference`
  (`r.colors color=differences`, diverging table)
- Species without a matching reference map are silently skipped for the diff
  step (the normal BD map is still written) and logged via `gs.message`
- Designed for seasonal/repeat monitoring: Mars CO2 polar cap retreat, Europa
  irradiation darkening, comet nucleus devolatilization between apparitions

**7.2 Statistical significance flagging (`change_sigma=`)**
- When both epochs were run with `radiometric_noise=`/`-e` (so both
  `<prefix>_<species>_unc` and `<reference_prefix>_<species>_unc` exist),
  pixels are tested at `|diff| / sqrt(σ_now² + σ_ref²) ≥ change_sigma`
  (default 2.0)
- Significant pixels are written to `<output_prefix>_<species>_diff_sig`
  (same diverging colour table); absent if either epoch lacks an uncertainty map
- JSON report gains `mean_diff`, `max_abs_diff`, `n_significant_change_pixels`
  (all `null`/`None` when `-d` is not used)

### Phase 4 — Advanced detection modes ✓ COMPLETE

**4.1 Spectral unmixing integration**
- Linear spectral unmixing (NNLS) for intimate mixtures:
  olivine + pyroxene typical on Moon/Mars; H2O ice + salt on Europa
- API: `p.matter.bands -u endmembers=<group>` — provide library endmembers
- Reference: Hapke 1993 mixing theory; Clark et al. 2003 (spectral unmixing)

**4.2 Band shape analysis (temperature correction)**
- Ice band position shifts with temperature (H2O: 2.02 µm shifts 5 nm/10 K)
- Relevant for Europa (90–130 K), Enceladus (80 K), Pluto (40 K)
- Correction: use `p.phocube` thermal model or provided temperature map
- Reference: Grundy & Schmitt 1998 (Icarus 130:178)

**4.3 Space weathering correction**
- Moon, Mercury, S-type asteroids: NPFe nanophase iron reduces band depth
  and reddens spectral slope (Hapke 2001; Clark et al. 2002)
- Correction model: `BD_corrected = BD_observed / (1 - α × Is/FeO)` (empirical)
- Would require Is/FeO map from lab-derived photometric parameters

**4.4 Atmospheric correction pipeline integration**
- For bodies with significant atmospheres (Mars, Venus, Titan):
  automatic `p.atcorr.hapke` pre-step before band depth computation
- `--atcorr` flag: run atmospheric correction in-pipeline using stored τ values

---

## DATABASE SCIENCE REFERENCES

The following peer-reviewed publications back every absorption band entry in
`data/matter_bands.json`. Only ISI/Scopus-indexed journals are used.

### Spectral laboratory foundations

| Ref | Relevance |
|---|---|
| Clark, R.N. & Roush, T.L. (1984). Reflectance spectroscopy: Quantitative analysis techniques for remote sensing applications. *J. Geophys. Res.* 89(B7):6329–6340. doi:10.1029/JB089iB07p06329 | Band depth formula used by p_spectra library |
| Clark, R.N. et al. (1990). High spectral resolution reflectance spectroscopy of minerals. *J. Geophys. Res.* 95(B8):12653–12680. doi:10.1029/JB095iB08p12653 | Primary spectral library: phyllosilicates, sulfates, carbonates, feldspar |
| Burns, R.G. (1993). *Mineralogical Applications of Crystal Field Theory* (2nd ed.). Cambridge. doi:10.1017/CBO9780511524899 | Electronic transition theory for olivine and pyroxene |
| Adams, J.B. (1974). Visible and near-infrared diffuse reflectance spectra of pyroxenes. *J. Geophys. Res.* 79(32):4829–4836. doi:10.1029/JB079i032p04829 | Pyroxene Band I/II parameters |
| Sunshine, J.M. & Pieters, C.M. (1993). Estimating modal abundances from spectra of natural and laboratory pyroxene mixtures using the modified Gaussian model. *J. Geophys. Res.* 98(E5):9075–9087. doi:10.1029/93JE00638 | Olivine deconvolution |

### Water ice and volatiles

| Ref | Relevance |
|---|---|
| Clark, R.N. (1981). The spectral reflectance of water-mineral mixtures at low temperatures. *J. Geophys. Res.* 86(B4):3087–3096. doi:10.1029/JB086iB04p03087 | H2O ice band positions |
| Clark, R.N. et al. (1986). Spectral reflectance of frost as a function of grain size and areal distribution. *J. Geophys. Res.* 91(B14):D233–D240. doi:10.1029/JD091iD02p0D233 | H2O/CO2/SO2 ice bands |
| Quirico, E. & Schmitt, B. (1997). Near-infrared spectroscopy of simple hydrocarbons and carbon oxides diluted in solid N2 and as pure ices. *Icarus* 127(2):354–378. doi:10.1006/icar.1997.5710 | N2, CO, CO2 volatile ice bands |
| Grundy, W.M. & Schmitt, B. (1998). The temperature-dependent near-infrared absorption spectrum of hexagonal H2O ice. *J. Geophys. Res.* 103(E11):25809–25822. doi:10.1029/98JE00738 | Temperature-dependent ice bands |

### Mars mineralogy

| Ref | Relevance |
|---|---|
| Pelkey, S.M. et al. (2007). CRISM multispectral summary products: Parameterizing mineral diversity on Mars from reflectance. *J. Geophys. Res.* 112(E8):E08S14. doi:10.1029/2006JE002831 | CRISM index definitions |
| Viviano-Beck, C.E. et al. (2014). Revised CRISM spectral parameters and summary products based on the currently detected mineral diversity on Mars. *J. Geophys. Res. Planets* 119(6):1403–1431. doi:10.1002/2014JE004627 | Updated Mars mineral indices |
| Ehlmann, B.L. & Edwards, C.S. (2014). Mineralogy of the Martian surface. *Annu. Rev. Earth Planet. Sci.* 42:291–315. doi:10.1146/annurev-earth-060313-055024 | Comprehensive Mars mineralogy review |
| Carter, J. et al. (2013). Hydrous minerals on Mars as seen by the CRISM and OMEGA imaging spectrometers: Updated global view. *J. Geophys. Res. Planets* 118(4):831–858. doi:10.1029/2012JE004145 | Phyllosilicate global mapping |
| Gendrin, A. et al. (2005). Sulfates in Martian layered terrains: The OMEGA/Mars Express view. *Science* 307:1587–1591. doi:10.1126/science.1109087 | Sulfate detection (kieserite/gypsum) |

### Outer solar system ices and organics

| Ref | Relevance |
|---|---|
| McCord, T.B. et al. (1998). Salts on Europa's surface detected by Galileo's NIMS spectrometer. *Science* 280:1242–1245. doi:10.1126/science.280.5367.1242 | Europa non-ice material detection |
| Carlson, R.W. et al. (1999). Hydrogen peroxide on the surface of Europa. *Science* 286:97–99. doi:10.1126/science.286.5437.97 | Europa H2O2 at 3.5 µm |
| Trumbo, S.K. et al. (2019). Sodium chloride on the surface of Europa. *Sci. Adv.* 5(6):eaaw7123. doi:10.1126/sciadv.aaw7123 | NaCl color centres on Europa |
| Smythe, W.D. et al. (1995). Absorption bands in the spectrum of Io and Io's atmosphere detected by the NIMS. *Icarus* 111(1):79–105. doi:10.1006/icar.1994.1138 | Io SO2 frost band assignments |
| Khare, B.N. et al. (1984). Optical constants of organic tholins produced in a simulated Titanian atmosphere. *Icarus* 60(1):127–137. doi:10.1016/0019-1035(84)90195-X | Tholin UV-VIS optical constants |
| Capaccioni, F. et al. (2015). The organic-rich surface of comet 67P/Churyumov-Gerasimenko as seen by VIRTIS/Rosetta. *Science* 347(6220):aaa0628. doi:10.1126/science.aaa0628 | Cometary organic-rich 3.2 µm feature |
| De Sanctis, M.C. et al. (2015). Ammoniated phyllosilicates with a likely outer Solar System origin on (1) Ceres. *Nature* 528:241–244. doi:10.1038/nature14334 | Ceres NH4 diagnostic band at 3.06 µm |

### Asteroids and small bodies

| Ref | Relevance |
|---|---|
| Kitazato, K. et al. (2019). The surface composition of asteroid 162173 Ryugu from Hayabusa2 near-infrared spectroscopy. *Science* 364:272–275. doi:10.1126/science.aav7432 | C-type 2.72/2.94 µm hydration |
| Hamilton, V.E. et al. (2019). Evidence for widespread hydrated minerals on asteroid (101955) Bennu. *Nature Astron.* 3:332–340. doi:10.1038/s41550-019-0894-3 | Bennu hydrated silicates |
| Yabuta, H. et al. (2023). Macromolecular organic matter in samples of the asteroid (162173) Ryugu. *Science* 379(6634):eabn9471. doi:10.1126/science.abn9471 | Ryugu organic matter (3.4 µm C-H) |

---

## ADDING CUSTOM SPECIES TO THE LOCAL DATABASE

On the first `p.in.*` import in a GRASS session, `p_meta` automatically copies
`$GISBASE/etc/planetary/matter_bands.json` to your current mapset:

```
$GISDBASE/$LOCATION/$MAPSET/Misc/matter_bands.json
```

`p.matter.bands` checks this mapset-local copy first.  You can edit it freely
to add unpublished species, mission-specific calibration data, or absorption
features from a paper that post-dates the system install — without touching the
system-wide file and without root access.

### Step 1 — locate (or seed) the local file

```bash
# After any p.in.* import the file is already there.
# To seed it manually before the first import:
MISC_DIR=$(g.gisenv get=GISDBASE)/$(g.gisenv get=LOCATION_NAME)/$(g.gisenv get=MAPSET)/Misc
mkdir -p "$MISC_DIR"
cp "$GISBASE/etc/planetary/matter_bands.json" "$MISC_DIR/matter_bands.json"
```

### Step 2 — understand the schema

A minimal species entry inside one body's matter-type array:

```json
{
  "name": "example_sulfate",
  "display_name": "Example Sulfate",
  "formula": "CaSO4·2H2O",
  "detection_range_um": [1.4, 2.5],
  "absorption_bands": [
    {
      "center": 1.45,
      "left":   1.35,
      "right":  1.55,
      "type":   "H2O combination",
      "sensors": ["CRISM", "OMEGA"]
    },
    {
      "center": 1.75,
      "left":   1.65,
      "right":  1.85,
      "type":   "S-O overtone",
      "sensors": ["CRISM"]
    },
    {
      "center": 2.21,
      "left":   2.10,
      "right":  2.35,
      "type":   "H2O+SO4 combination",
      "sensors": ["CRISM", "OMEGA"]
    }
  ],
  "refs": [
    {
      "cite": "Author et al. (2024) Planet. Sci. J. X:Y",
      "doi":  "10.3847/PSJ/XXXXX"
    }
  ]
}
```

**Required fields**: `name` (unique within the body), `absorption_bands[]` with
at least `center`, `left`, `right` (all in µm).  
**Optional**: `display_name`, `formula`, `detection_range_um` (inferred from
band extremes if absent), `sensors[]`, `refs[]`.

### Step 3 — insert the new entry

Open the file in any editor and append to the appropriate array.  Example
adding a Mars mineral (abbreviated JSON showing insertion point):

```json
{
  "bodies": {
    "mars": {
      "minerals": [
        { ... existing species ... },
        {
          "name": "mg_perchlorate_hexahydrate",
          "display_name": "Mg-perchlorate hexahydrate",
          "formula": "Mg(ClO4)2·6H2O",
          "detection_range_um": [1.4, 3.5],
          "absorption_bands": [
            {
              "center": 1.47,
              "left":   1.35,
              "right":  1.60,
              "type":   "H2O combination band",
              "sensors": ["CRISM", "MRO-CRISM"]
            },
            {
              "center": 1.98,
              "left":   1.85,
              "right":  2.10,
              "type":   "H2O combination band",
              "sensors": ["CRISM"]
            },
            {
              "center": 2.14,
              "left":   2.05,
              "right":  2.25,
              "type":   "Cl-O overtone + H2O",
              "sensors": ["CRISM"]
            }
          ],
          "refs": [
            {
              "cite": "Hanley et al. (2015) GRL 42:687–694",
              "doi":  "10.1002/2014GL062403"
            }
          ]
        }
      ]
    }
  }
}
```

### Step 4 — validate the JSON

```bash
python3 -m json.tool "$MISC_DIR/matter_bands.json" > /dev/null && echo "JSON valid"
```

### Step 5 — verify the module sees the new species

```bash
p.matter.bands -l group=<your_group> body=mars matter=minerals \
    wavelengths=<your_wavelengths.csv>
```

The new species appears in the `Detectable` list if the sensor range covers its
`detection_range_um`.  If it appears under `Out of sensor range`, the sensor
does not cover the required wavelengths — the entry is correct but the data
cannot detect it.

### Step 6 — run the detection

```bash
p.matter.bands group=crism_scene body=mars matter=minerals \
    output_prefix=my_mars wavelengths=crism_wavelengths.csv \
    min_bd=0.02 -v
```

The new species produces a map `my_mars_mg_perchlorate_hexahydrate` with band
depth values in [0, 1].

### Notes on the `wavelengths=` CSV

One row per band in the image group, matching band order:

```csv
# wavelength_um, fwhm_um
1.0020, 0.0065
1.0085, 0.0065
1.0150, 0.0065
...
```

FWHM is used only for display and documentation; it does not affect band depth
computation.

---

## USAGE EXAMPLES

### List detectable species for Europa given NIMS-range data

```bash
p.matter.bands -l group=europa_nims body=europa
```

Output:
```
Europa — sensor coverage 0.70–5.20 µm (NIMS)
Detectable (8):  water_ice_crystalline, hydrated_sulfuric_acid, epsomite,
                 so2_surface, co2_surface, hydrogen_peroxide, sodium_chloride*
Skipped (1):     sodium_chloride [needs UV < 0.50 µm; not covered]
* single-feature detection only
```

### Map minerals on Mars from CRISM FRT

```bash
# Import and pre-process CRISM product (existing modules)
p.in.pds3 input=FRT00003E12_07_IF166L_TRR3.img output=crism
p.atcorr.hapke input=crism output=crism_corr tau=0.3 omega=0.92 g=0.3

# Map all mineral species
p.matter.bands group=crism_corr body=mars matter=minerals \
    output_prefix=mars_FRT3E12 min_bd=0.02 -vc
```

Output maps:
```
mars_FRT3E12_olivine            (BD at 1.05 µm)
mars_FRT3E12_low_ca_pyroxene    (BD composite 0.92+1.85 µm)
mars_FRT3E12_high_ca_pyroxene   (BD composite 1.00+2.30 µm)
mars_FRT3E12_smectite_montmorillonite  (BD composite 1.41+1.91+2.21 µm)
mars_FRT3E12_nontronite         (BD composite 1.41+1.91+2.29 µm)
mars_FRT3E12_gypsum             (BD composite 1.45+1.75+1.95+2.22 µm)
... (13 more mineral maps)
mars_FRT3E12_composite_RGB      (R=olivine, G=smectite, B=hematite)
```

### Map Titan surface organics from Cassini VIMS

```bash
p.in.pds3 input=v1722289127_1_vis.qub output=vims_vis band=96
# (import all bands, build group)
p.matter.bands group=vims_titan body=titan matter=organics,liquids \
    output_prefix=titan_vims -c
```

### Detect ices on Pluto from LEISA

```bash
p.matter.bands group=leisa_pluto body=pluto matter=ices \
    output_prefix=pluto_nh min_bd=0.05 -lv
```

### Full pipeline — HiRISE colour over Mawrth Vallis clay outcrops (Mars)

Mawrth Vallis (≈22–24°N, 341–344°E) is one of the mineralogically richest
phyllosilicate terrains on Mars, hosting Fe/Mg smectites (nontronite, Fe-smectite)
overlain by Al-phyllosilicates (montmorillonite, kaolinite), all diagnostic of
prolonged aqueous alteration in the Noachian (Loizeau et al. 2010; Bishop et al.
2008).  HiRISE Enhanced Colour images (three bands: BG, RED, NIR) provide
25 cm/pixel morphological context and allow detection of Fe-oxide signatures;
phyllosilicate absorption bands at 1.4, 1.9 and 2.2 µm lie beyond HiRISE's
wavelength range but are mapped simultaneously by CRISM (see note at end).

```bash
# ── 0. One-time: create a Mars geographic GRASS location ─────────────────────
# IAU Mars 2000 geographic CRS (lat/lon in degrees, body radius 3396.19 km)
grass -c EPSG:49900 ~/grassdata/mars

# ── 1. Set the working region to Mawrth Vallis clay outcrop centre ───────────
# The main Fe/Mg smectite exposure described in Bishop et al. (2008)
# is centred near 23.6°N, 342.8°E; 0.5° box at 1 arcsec resolution.
g.region n=23.85 s=23.35 e=343.05 w=342.55 res=0:00:01

# ── 2. List available HiRISE Enhanced Colour products in this region ─────────
p.in.astropedia -l search="HiRISE Enhanced Color Mawrth" limit=20

# ── 3. Import the three HiRISE colour bands ───────────────────────────────────
# HiRISE Enhanced Colour products expose three bands on Astropedia STAC:
#   Band 1 — BG  (Blue-Green, ~400–600 nm, centre 500 nm)
#   Band 2 — RED (Red,        ~550–850 nm, centre 700 nm)
#   Band 3 — NIR (Near-IR,   ~750–1000 nm, centre 875 nm)
# (McEwen et al. 2007 SSR 129:369; Delamere et al. 2010 SSR 150:477)
#
# Using observation PSP_002074_2025 (2006-12-04, 23.6°N 342.8°E,
# centred on the main Fe/Mg smectite terrace).
p.in.astropedia search="PSP_002074_2025" band=1 output=mawrth_bg  --overwrite
p.in.astropedia search="PSP_002074_2025" band=2 output=mawrth_red --overwrite
p.in.astropedia search="PSP_002074_2025" band=3 output=mawrth_nir --overwrite

# ── 4. Build an image group ───────────────────────────────────────────────────
i.group group=mawrth_hirise input=mawrth_bg,mawrth_red,mawrth_nir

# ── 5. Create the HiRISE wavelength CSV ──────────────────────────────────────
# Two-column format: wavelength_um, fwhm_um  (one row per band in group order)
cat > hirise_wavelengths.csv << 'EOF'
# HiRISE Enhanced Colour filter wavelengths (McEwen et al. 2007)
# wavelength_um, fwhm_um
0.500, 0.200
0.700, 0.300
0.875, 0.125
EOF

# ── 6. Check what is detectable at HiRISE wavelengths ───────────────────────
# Expect: Fe-oxide (hematite broad ~0.53/0.86 µm) within range.
# Expect: all phyllosilicates (1.4, 1.9, 2.2 µm) OUT of range — HiRISE
# cannot detect them; those entries appear under "Out of sensor range".
p.matter.bands -lv \
    group=mawrth_hirise \
    body=mars \
    output_prefix=mawrth \
    wavelengths=hirise_wavelengths.csv

# Example output:
#   Body: mars | Bands: 3 | Sensor: 0.5000–0.8750 µm
#   Detectable (2):
#     hematite           minerals   0.5330 µm, 0.8600 µm
#     goethite           minerals   0.5300 µm, 0.6600 µm
#   Out of sensor range (14):
#     olivine            needs 0.8000–2.5000 µm
#     low_ca_pyroxene    needs 0.9000–2.0000 µm
#     smectite_fe_mg     needs 1.3500–2.5000 µm
#     nontronite         needs 1.3500–2.5000 µm
#     montmorillonite    needs 1.3500–2.5000 µm
#     ... (9 more)

# ── 7. Compute Fe-oxide band depth maps ─────────────────────────────────────
p.matter.bands \
    group=mawrth_hirise \
    body=mars \
    matter=minerals \
    output_prefix=mawrth \
    wavelengths=hirise_wavelengths.csv \
    min_bd=0.02 \
    -vc

# Output maps:
#   mawrth_hematite   — BD composite 0.53 + 0.86 µm [DCELL, 0–1]
#   mawrth_goethite   — BD composite 0.53 + 0.66 µm [DCELL, 0–1]
#   mawrth_composite_RGB — R=hematite, G=0, B=0

# ── 8. Display ────────────────────────────────────────────────────────────────
r.colors map=mawrth_hematite color=reds
d.mon start=wx0
d.rast mawrth_hematite
d.rast mawrth_red  # true-colour context underneath
```

**Interpreting the output** — high `mawrth_hematite` band depth (~0.05–0.25)
marks the grey hematite-rich cap that conformably overlies the phyllosilicate
sequence visible in CRISM data (Loizeau et al. 2010).  The phyllosilicate
layers underneath are spatially co-located but require CRISM wavelength
coverage for detection.

**For complete clay mapping — co-register with CRISM TRDR**:

```bash
# CRISM covers 0.36–3.92 µm in 544 bands — detects all phyllosilicate features
# Fetch a CRISM Full Resolution Targeted Reduced Data Record over the same area
p.in.astropedia search="CRISM FRT Mawrth" limit=5

# Import (example product FRT00003BFB_07_IF166L_TRRU)
p.in.pds3 \
    input=FRT00003BFB_07_IF166L_TRRU.img \
    output=mawrth_crism

# Build CRISM group and wavelength CSV from the label
# (CRISM BandBin centres extracted from .lbl with grep/awk)
awk '/BAND_BIN_CENTER/{found=1} found{print; if(/}/) exit}' \
    FRT00003BFB_07_IF166L_TRRU.lbl \
    | grep -oP '[0-9]+\.[0-9]+' \
    | awk '{print $1 ", 0.0065"}' > crism_wavelengths.csv

# Detect all Mars minerals + ices from CRISM
p.matter.bands \
    group=mawrth_crism \
    body=mars \
    matter=minerals,ices \
    output_prefix=mawrth_crism \
    wavelengths=crism_wavelengths.csv \
    min_bd=0.02 \
    -vc
# → mawrth_crism_nontronite, mawrth_crism_montmorillonite,
#   mawrth_crism_smectite_fe_mg, mawrth_crism_kaolinite …

# Co-register HiRISE Fe-oxide map with CRISM clay map for combined analysis
r.resamp.interp input=mawrth_hematite output=mawrth_hematite_crism_res \
    method=bilinear
```

**References** for this example:
- Bishop, J.L. et al. (2008). Phyllosilicate diversity and past aqueous activity
  revealed at Mawrth Vallis, Mars. *Science* 321:830–833.
  doi:[10.1126/science.1159699](https://doi.org/10.1126/science.1159699)
- Loizeau, D. et al. (2010). Stratigraphy over the Mawrth Vallis region through
  OMEGA, HRSC colour imagery and DTM. *Icarus* 205:396–418.
  doi:[10.1016/j.icarus.2009.04.018](https://doi.org/10.1016/j.icarus.2009.04.018)
- McEwen, A.S. et al. (2007). Mars Reconnaissance Orbiter's High Resolution
  Imaging Science Experiment (HiRISE). *Space Sci. Rev.* 129:369–397.
  doi:[10.1007/s11214-007-9177-4](https://doi.org/10.1007/s11214-007-9177-4)

### Full pipeline — CRISM TRDR over Mawrth Vallis clay outcrops (Mars)

Mawrth Vallis (≈22–24°N, 341–344°E) is the best-studied phyllosilicate terrain
on Mars and a former Mars 2020 landing-site candidate.  The stratigraphic
sequence — Fe/Mg smectites (nontronite, Fe-saponite) at depth, overlain by
Al-phyllosilicates (montmorillonite, kaolinite) — is uniquely complete and fully
accessible to orbital spectroscopy (Bishop et al. 2008; Loizeau et al. 2010;
Carter et al. 2015).

The four orbital instruments that detect minerals on the Martian surface are:
- **TES** (Mars Global Surveyor) — thermal emission 6–50 µm, ≥3 km/pixel
- **OMEGA** (Mars Express) — reflected + thermal 0.36–5.1 µm, 300 m–4.8 km/pixel
- **CRISM** (MRO) — reflected 0.36–3.92 µm, 15–18 m/pixel in targeted mode
- **MMS** (Tianwen-1) — reflected 0.38–3.2 µm, 60 m/pixel

CRISM TRDR (Targeted Reduced Data Record) provides the best spatial resolution
for clay-unit mapping and is the standard reference dataset for Mawrth Vallis.
The IR (S-detector) channel covers 1.00–3.92 µm in 438 bands, resolving all
phyllosilicate absorption features.

```bash
# ── 0. One-time: GRASS location on Mars geographic CRS (IAU 2015) ────────────
grass -c EPSG:49900 ~/grassdata/mars

# ── 1. Set region to the main Mawrth Vallis clay terrace ─────────────────────
# Bishop et al. (2008) Science Fig. 1 centre: 23.6°N, 342.8°E
# 0.2° box encloses the Fe/Mg → Al phyllosilicate sequence
g.region n=23.70 s=23.50 e=342.90 w=342.70 res=0:00:02

# ── 2. List CRISM targeted products intersecting this region ─────────────────
p.in.astropedia -lr \
    opus="instrument=MRO CRISM,target=Mars" \
    output=dummy limit=15

# ── 3. Find and import a CRISM IR TRDR ───────────────────────────────────────
# Two complementary resources identify suitable CRISM tiles for Mawrth Vallis:
#
#   • rsidea.whu.edu.cn/Martian_mineral_detection.htm — WHU group CRISM
#     mineral-detection dataset (31-class MICA-based classification; ISPRS
#     J. Photogramm. Remote Sens. 2024, doi:10.1016/j.isprsjprs.2024.03587).
#     The dataset lists CRISM FRT tiles and their mineral content; filter for
#     tiles intersecting 22–24°N, 341–344°E and showing phyllosilicate classes.
#
#   • OPUS browser (opus.pds-rings.seti.org) — search by instrument
#     MRO CRISM, target Mars, lon 342.55–342.95, lat 23.35–23.85 to list all
#     CRISM observations with footprint, emission angle, and solar longitude.
#     Prefer low emission angle (<10°) and low dust opacity (Ls < 180° for
#     northern autumn when dust loading is minimal).
#
# CRISM TRDR PDS naming convention:
#   FRT<8-hex-obsid>_07_IF<mode><channel>_TRR3
#   channel S = IR detector (1.0–3.92 µm, 438 bands)
#   channel L = VNIR detector (0.36–1.05 µm, 107 bands)
#
# Well-documented tiles over the Mawrth Vallis clay terrace:
#   FRT00003BFB  2006-11-02  23.6°N 342.8°E  — primary phyllosilicate exposure
#   FRT0000CBCE  2009-03-15  23.5°N 343.0°E  — northern terrace margin
#   FRT0000ABD6  2008-08-22  23.7°N 342.6°E  — cross-section through Al/Fe units

# List IR TRDR products that intersect the working region (OPUS search):
p.in.astropedia -lr \
    opus="instrument=MRO+CRISM,target=Mars" \
    output=dummy limit=20

# Import the chosen IR tile.
# p.in.astropedia downloads the .img cube + .lbl from OPUS and dispatches to
# p.in.pds3, which imports each band as a separate raster: mawrth_crism_ir.1 …
CRISM_TILE="mro-crism-frt00003bfb_07_if166s_trr3"   # set to chosen tile OPUS ID
p.in.astropedia \
    opus_id="${CRISM_TILE}" \
    output=mawrth_crism_ir \
    --overwrite

# ── 4. Build the image group ──────────────────────────────────────────────────
# All 438 S-detector bands in wavelength order (p.in.pds3 preserves band order)
BAND_MAPS=$(g.list type=raster pattern="mawrth_crism_ir.*" \
    separator=comma mapset=.)
i.group group=mawrth_crism input="${BAND_MAPS}"

# ── 5. Extract CRISM IR wavelengths from the PDS3 label ──────────────────────
# BAND_BIN_CENTER in the TRDR .lbl lists centre wavelengths in nm.
# The one-liner below extracts them and converts to µm for the CSV.
python3 - << 'PYEOF'
import re, os

lbl = "FRT00003BFB_07_IR166S_TRR3.LBL"
wl_nm = [float(x) for x in
         re.findall(r"[\d.]+", "".join(
             open(lbl).read().split("BAND_BIN_CENTER")[1].split(")")[0]
             .split("(")[1:]))]
with open("crism_ir_wavelengths.csv", "w") as f:
    f.write("# CRISM S-detector wavelengths (Murchie et al. 2007 JGR)\n")
    f.write("# wavelength_um, fwhm_um\n")
    for wl in wl_nm:
        f.write(f"{wl/1000.0:.6f}, 0.00650\n")
print(f"Wrote {len(wl_nm)} bands: {wl_nm[0]/1000:.4f}–{wl_nm[-1]/1000:.4f} µm")
PYEOF
# → Wrote 438 bands: 1.0020–3.9192 µm

# ── 6. Verify detectable species ─────────────────────────────────────────────
p.matter.bands -lv \
    group=mawrth_crism \
    body=mars \
    output_prefix=mawrth \
    wavelengths=crism_ir_wavelengths.csv

# Expected output (representative):
#   Body: mars | Bands: 438 | Sensor: 1.0020–3.9192 µm
#   Detectable (14):
#     olivine              minerals  1.0500 µm
#     low_ca_pyroxene      minerals  0.9200+1.8500 µm
#     high_ca_pyroxene     minerals  1.0000+2.3000 µm
#     smectite_fe_mg       minerals  1.4100+1.9100+2.2900 µm   ← nontronite
#     nontronite           minerals  1.4100+1.9100+2.2900 µm   ← primary target
#     montmorillonite      minerals  1.4100+1.9100+2.2100 µm   ← Al-phyllosil.
#     kaolinite            minerals  1.4100+1.9100+2.2100 µm
#     chlorite             minerals  2.2500+2.3300 µm
#     serpentine           minerals  2.3200+2.5200 µm
#     gypsum               minerals  1.4500+1.7500+1.9500 µm
#     jarosite             minerals  0.4300+0.9000 µm
#     hematite             minerals  0.5330+0.8600 µm
#     h2o_ice              ices      1.5000+2.0200 µm
#     co2_ice              ices      1.4300+2.0100 µm
#   Out of range (2): perchlorate_hydrate, ...

# ── 7. Map all Mars minerals and ices ────────────────────────────────────────
p.matter.bands \
    group=mawrth_crism \
    body=mars \
    matter=minerals,ices \
    output_prefix=mawrth \
    wavelengths=crism_ir_wavelengths.csv \
    min_bd=0.02 \
    -vc

# Output maps (one per detected species):
#   mawrth_nontronite           BD composite 1.41 + 1.91 + 2.29 µm
#   mawrth_montmorillonite      BD composite 1.41 + 1.91 + 2.21 µm
#   mawrth_kaolinite            BD composite 1.41 + 1.91 + 2.21 µm
#   mawrth_smectite_fe_mg       BD composite 1.41 + 1.91 + 2.29 µm
#   mawrth_chlorite             BD composite 2.25 + 2.33 µm
#   mawrth_serpentine           BD composite 2.32 + 2.52 µm
#   mawrth_low_ca_pyroxene      BD composite 0.92 + 1.85 µm
#   mawrth_olivine              BD 1.05 µm
#   mawrth_hematite             BD composite 0.53 + 0.86 µm
#   mawrth_gypsum               BD composite 1.45 + 1.75 + 1.95 µm
#   mawrth_h2o_ice              BD composite 1.50 + 2.02 µm
#   mawrth_composite_RGB        i.group: R=nontronite, G=montmorillonite, B=olivine

# ── 8. Colour tables and display ─────────────────────────────────────────────
r.colors map=mawrth_nontronite      color=reds
r.colors map=mawrth_montmorillonite color=blues
r.colors map=mawrth_kaolinite       color=greens

d.mon start=wx0 resolution=1
d.rast mawrth_composite_RGB
# Red pixels  → nontronite (Fe/Mg smectite layer, lower stratigraphic unit)
# Blue pixels → montmorillonite (Al-phyllosilicate cap, upper unit)
# Mixed areas → transition zone; cross-reference with HRSC/HiRISE DTM

# ── 9. Extract stratigraphy transect ─────────────────────────────────────────
# Profile through the phyllosilicate terrace (N–S transect at 342.80°E)
r.profile \
    input=mawrth_nontronite,mawrth_montmorillonite,mawrth_kaolinite \
    coordinates="342.80,23.70,342.80,23.50" \
    output=mawrth_clay_transect.csv
```

**Interpreting the result** — the stratigraphic trend expected from Bishop et al.
(2008) and Loizeau et al. (2010): nontronite (2.29 µm) band depth peaks in the
lower terrace unit (Noachian, pre-3.7 Ga); montmorillonite/kaolinite (2.21 µm)
band depth peaks in the overlying 10–200 m cap.  The `mawrth_composite_RGB`
map reproduces this as a red-to-blue vertical gradient across the terrace edge.

**References**:
- Bishop, J.L. et al. (2008). Phyllosilicate diversity and past aqueous activity
  revealed at Mawrth Vallis, Mars. *Science* 321:830–833.
  doi:[10.1126/science.1159699](https://doi.org/10.1126/science.1159699)
- Carter, J. et al. (2015). Widespread surface weathering on early Mars:
  A case for a warmer and wetter climate. *Icarus* 248:373–382.
  doi:[10.1016/j.icarus.2014.11.011](https://doi.org/10.1016/j.icarus.2014.11.011)
- Loizeau, D. et al. (2010). Stratigraphy over the Mawrth Vallis region through
  OMEGA, HRSC colour imagery and DTM. *Icarus* 205:396–418.
  doi:[10.1016/j.icarus.2009.04.018](https://doi.org/10.1016/j.icarus.2009.04.018)
- Murchie, S. et al. (2007). Compact Reconnaissance Imaging Spectrometer for
  Mars (CRISM) on Mars Reconnaissance Orbiter (MRO). *J. Geophys. Res.*
  112:E05S03. doi:[10.1029/2006JE002682](https://doi.org/10.1029/2006JE002682)
- Viviano-Beck, C.E. et al. (2014). Revised CRISM spectral parameters and
  summary products. *J. Geophys. Res. Planets* 119:1403–1431.
  doi:[10.1002/2014JE004627](https://doi.org/10.1002/2014JE004627)
- WHU Remote Sensing Ideas Group (2024). Martian mineral detection from CRISM
  hyperspectral imagery. *ISPRS J. Photogramm. Remote Sens.*
  doi:[10.1016/j.isprsjprs.2024.03587](https://doi.org/10.1016/j.isprsjprs.2024.03587)
  — dataset and tile index: <http://rsidea.whu.edu.cn/Martian_mineral_detection.htm>

---

## PRE-PROCESSING CHAIN (required before p.matter.bands)

```
Raw PDS data
    │
    ▼
p.in.pds3 / p.in.pds4 / p.in.isis  ← import, sets wavelength metadata
    │
    ▼ (if needed)
p.specpix                            ← clean special pixel values
p.dstripe / p.cubenorm               ← remove detector artifacts
    │
    ▼ (always)
p.albedo                             ← convert I/F → geometric albedo
    │
    ▼ (if atmosphere)
p.atcorr.hapke                       ← remove atmospheric contribution
    │
    ▼ (optional for photometric uniformity)
p.phocube → p.photomet               ← normalise incidence/emission
    │
    ▼
p.matter.bands                       ← absorption band detection maps
```

---

## SEE ALSO

- [p.mineral.indices](../p.mineral.indices/p.mineral.indices.md) — Mars-specific CRISM indices (predecessor)
- [p.spectral.planet](../p.spectral.planet/p.spectral.planet.md) — generic spectral operations
- [p.atcorr.hapke](../p.atcorr.hapke/p.atcorr.hapke.md) — atmospheric correction
- [p.photomet](../p.photomet/p.photomet.md) — photometric correction
- [p.phocube](../p.phocube/p.phocube.md) — geometry backplanes
- [p.in.pds3](../p.in.pds3/p.in.pds3.md) — import hyperspectral PDS3 cubes

## AUTHOR

Yann Chemin

## STATUS

Phases 1–7 complete.

Phase 1: band database (`data/matter_bands.json`), C library extension
(`p_spectra_bd_multi`, `p_spectra_apply_row_bd_multi`), Python module
(`p.matter.bands.py`), and auto-install via `p_meta_install_matter_bands()`.

Phase 2: wavelength range extended to 0.18–200 µm.  UV coverage (15 band
entries across 8 bodies); MIR emissivity mode (`mode=emissivity`) with 38 band
features for TES/THEMIS/MERTIS/JWST-MIRI sensors; LWIR/FIR cometary H2O
rotational lines (Herschel/PACS, HIFI, ALMA).

Phase 3: 6 new bodies (ganymede, callisto, triton, ariel, uranus_moon,
asteroid_d_type) + expansion of Europa, Titan, Venus.  Database now holds
123 species across 19 bodies, wavelength range 0.18–200 µm.  Testsuite
has 46 tests across three test classes (Phase 1: 11, Phase 2: 15, Phase 3: 20).

Phase 4: advanced detection modes — NNLS spectral unmixing (`-u endmembers=`,
one full-spectrum image group per endmember); temperature-dependent ice
band-center correction (`temperature=`); space weathering correction
(`space_weathering=`, body-specific α from `body_meta` in the database);
Hapke atmospheric correction pre-step (`-a atcorr_*=`). Phase 4 adds 13
testsuite tests (class TestPmatterbandsPhase4; total: 59 tests).

Phase 5: confidence & quality outputs — per-species band-concordance confidence
raster (`-q`, value = n_bands_matched / n_diagnostic_bands); minimum confidence
filter (`min_conf=`, default 0.0) to suppress under-constrained detections;
structured JSON detection report (`report=`) listing per-species confidence,
mean/max BD, n_matched/n_total, and skipped-species reasons; composite RGB
channel selection upgraded from mean-BD to confidence × mean-BD weighting.
Phase 5 adds 11 testsuite tests (class TestPmatterbandsPhase5; total: 70 tests).

Phase 6: classification and uncertainty propagation — dominant-species
classification raster (`-k`, `<prefix>_classification`, category code of the
highest confidence-weighted BD species per pixel, with `r.category` labels);
radiometric uncertainty propagation (`radiometric_noise=` + `-e`, analytic
first-order error propagation through the band-depth formula, written as
`<prefix>_<species>_unc`); JSON report gains a `mean_uncertainty` field.
Phase 6 adds 10 testsuite tests (class TestPmatterbandsPhase6; total: 80 tests).

Phase 7: multi-temporal change detection — band-depth difference map (`-d`,
`reference_prefix=`, `<prefix>_<species>_diff = BD_now − BD_reference`,
diverging colour table); statistical significance flagging (`change_sigma=`,
default 2.0σ) using combined uncertainty from both epochs when available,
written to `<prefix>_<species>_diff_sig`; JSON report gains `mean_diff`,
`max_abs_diff`, `n_significant_change_pixels`. Phase 7 adds 10 testsuite
tests (class TestPmatterbandsPhase7; total: 90 tests).
