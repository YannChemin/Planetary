# Vendored CSPICE sources

This directory contains a **subset** of the NASA/JPL NAIF CSPICE toolkit,
vendored into the repository so `planetary-cspice` builds reproducibly with no
external source tree.

- **Toolkit version:** N0067
- **Upstream:** https://naif.jpl.nasa.gov/pub/naif/toolkit/C/
  (PC_Linux_GCC_64bit package `cspice.tar.Z`)
- **Included:**
  - `src/cspice/` — the CSPICE library C sources (2229 `.c`) and their local
    headers, sufficient to build `libcspice.so`.
  - `include/` — the public SPICE headers (`SpiceUsr.h` et al.).
- **Excluded** (not needed to build the shared library): the upstream
  command-line utility sources (`brief_c`, `mkdsk_c`, …), prebuilt
  `lib/*.a`, `exe/`, `data/`, and `doc/`.

## Planetary SPICE utilities (`cspice-pkg/utils/`)

Planetary ships four purpose-built replacements for the excluded NAIF
utilities, written in C against `libcspice.so`:

| Binary | NAIF equivalent | Purpose |
|---|---|---|
| `spice-brief` | `brief` | SPK / binary PCK coverage summary |
| `spice-ckbrief` | `ckbrief` | CK (pointing) coverage summary |
| `spice-chronos` | `chronos` | Time conversion: UTC ↔ ET ↔ SCLK |
| `spice-commnt` | `commnt -r` | Print comment area of DAF kernels |

Built by `make utils` (or `make all`); installed to `$(INST_DIR)/bin/`.

## License

CSPICE is produced by NASA/JPL NAIF and distributed under the NAIF license
(permissive, but not OSI/DFSG-approved). See `../DEBIAN/copyright` for the
statement and packaging notes. The vendored files are unmodified upstream
sources.

## Updating

To refresh to a newer toolkit, replace `src/cspice/*.c`, `src/cspice/*.h` and
`include/*.h` from the upstream package and update the version above.
