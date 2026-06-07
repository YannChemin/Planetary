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
- **Excluded** (not needed to build the shared library): the command-line
  utility sources (`brief_c`, `mkdsk_c`, …), prebuilt `lib/*.a`, `exe/`,
  `data/`, and `doc/`.

## License

CSPICE is produced by NASA/JPL NAIF and distributed under the NAIF license
(permissive, but not OSI/DFSG-approved). See `../DEBIAN/copyright` for the
statement and packaging notes. The vendored files are unmodified upstream
sources.

## Updating

To refresh to a newer toolkit, replace `src/cspice/*.c`, `src/cspice/*.h` and
`include/*.h` from the upstream package and update the version above.
