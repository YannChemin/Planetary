# Installation

## Overview

Two packages are produced from this source tree:

1. **`planetary-cspice`** — NAIF CSPICE N0067 compiled as `libcspice.so`,
   built from the vendored `cspice-pkg/` directory.
2. **`grass-planetary-addons`** — the 55 GRASS addon modules.
   Carries `Pre-Depends: planetary-cspice (>= <shared-version>)`, so `dpkg`
   enforces install order automatically.

---

## Source tree layout

```
Planetary/
├── planetary/          55 p.*/g.* modules (GRASS standard category subdir)
│   ├── Makefile        category Makefile — MODULE_TOPDIR ?= $(HOME)/dev/grass
│   ├── p_lib.py        shared Python library
│   ├── p_spice.py      shared Python library
│   └── p.*/            one subdir per module, each with its own Makefile
├── libs/               7 private C libraries (compiled on demand by modules)
├── bodies/             body JSON files (Moon, Mars, Venus)
├── missions/           27 mission JSON files
├── cspice-pkg/         vendored NAIF CSPICE N0067 sources
└── debian/             Debian packaging
```

The `planetary/` directory follows the same convention as `grass-addons/src/raster/`,
`grass-addons/src/imagery/`, etc. Every module Makefile sets
`MODULE_TOPDIR ?= $(HOME)/dev/grass` and is overridden by the command line.

---

## Option A — Debian packages (recommended for testing/deployment)

### Configure paths (first time only)

The build system reads a gitignored `config.mk` for local path overrides.
Copy the example and edit it to match your setup:

```bash
cd ~/dev/Planetary
cp config.mk.example config.mk
$EDITOR config.mk
```

`config.mk` variables and their defaults:

| Variable | Default | Purpose |
|----------|---------|---------|
| `MODULE_TOPDIR` | `$(HOME)/dev/grass` | GRASS source tree used to compile modules |
| `GRASS_ADDONS_TOPDIR` | `$(HOME)/dev/grass-addons` | grass-addons tree (used by `update.sh`) |
| `GRASS_PREFIX` | auto-detected from `/usr/local/grass*/` | Installed GRASS prefix (`make install` target) |

If your GRASS source tree is at the default location (`~/dev/grass`) you can
skip this step entirely — the Makefile defaults work out of the box.

### Build

Always start with a clean slate to prevent GRASS version hash mismatches:

```bash
cd ~/dev/Planetary
make clean && make deb
```

`make clean` removes all OBJ.* build directories, compiled binaries,
Python bytecode, and Debian staging trees.

`make deb`:
1. Stamps a datetime suffix onto the version (`1.0.0` → `1.0.0+YYYYMMDDHHMMSS`)
   so every rebuild produces a strictly higher version — `dpkg -i` always
   overwrites installed files without needing to remove the old package first.
2. Compiles `libcspice.so` from ~2 266 vendored NAIF C sources (`-std=gnu89`).
3. Builds all GRASS C modules via the standard GRASS build system.
4. Builds `p.sunmask` + `libpsunmask.so` via its standalone Makefile.
5. Builds `p.horizon.gpu` via the GRASS build system (GDAL + OpenMP + OpenCL).
6. Packages all Python script modules (no compilation needed).

Both `.deb` files land one directory up (`~/dev/`).

### Install

```bash
cd ~/dev
sudo dpkg -i planetary-cspice_1.0.0+<timestamp>_amd64.deb
sudo dpkg -i grass-planetary-addons_1.0.0+<timestamp>_amd64.deb
```

### What gets installed

`planetary-cspice`:
- `/usr/local/lib/libcspice.so` — NAIF CSPICE shared library
- `/usr/local/include/cspice/*.h` — C headers

`grass-planetary-addons`:
- `/usr/lib/grass/addons/bin/` — compiled C module executables
- `/usr/lib/grass/addons/scripts/` — Python script modules
- `/usr/lib/grass/addons/p_lib.py`, `p_spice.py` — shared Python libraries
- `/usr/lib/grass/addons/docs/html/` — HTML manuals
- `/usr/lib/grass/addons/docs/man/man1/` — man pages
- `/usr/lib/grass/addons/g.gui.landing/landing_wizard_wx.py` — wxPython wizard
- `/usr/lib/grass/addons/landing.qt/p_landing_qt.py` — Qt6 wizard
- `/usr/lib/grass/addons/bin/p-landing-qt` — symlink to Qt6 wizard
- `/usr/lib/grass/addons/bodies/` — body JSON files
- `/usr/lib/grass/addons/missions/` — mission JSON files
- `/usr/lib/x86_64-linux-gnu/libpsunmask.so` — ctypes shadow-cast library

GRASS adds both `$GRASS_ADDON_BASE/bin/` and `$GRASS_ADDON_BASE/scripts/`
to PATH inside every GRASS session, so all modules are accessible by name.

---

## Option B — Direct install alongside GRASS and grass-addons

This follows the same workflow as `$HOME/dev/update.sh`.
Configure `config.mk` as described in Option A before running these commands.

### Build

```bash
make clean
make -j8
```

Or override `MODULE_TOPDIR` on the command line if you have not created `config.mk`:

```bash
make MODULE_TOPDIR=$HOME/dev/grass clean
make MODULE_TOPDIR=$HOME/dev/grass -j8
```

This compiles all modules into the GRASS dist tree at
`$HOME/dev/grass/dist.x86_64-pc-linux-gnu/`.

### Install

Run GRASS's system-wide install first (which picks up all dist-tree contents
including Planetary's modules), then install the extras:

```bash
cd $HOME/dev/grass
sudo make install            # installs to /usr/local/grass86/ (or GRASS_PREFIX)

cd $HOME/dev/Planetary
sudo make install            # installs p_lib.py, bodies/, missions/ to INST_DIR
```

If `config.mk` is not present, pass the paths explicitly:

```bash
sudo make MODULE_TOPDIR=$HOME/dev/grass INST_DIR=/usr/local/grass86 install
```

`make install` in Planetary installs:
- `$(INST_DIR)/bin/` and `$(INST_DIR)/scripts/` — all module executables (via GRASS install)
- `$(INST_DIR)/p_lib.py`, `$(INST_DIR)/p_spice.py` — shared Python libraries
- `$(INST_DIR)/planetary/bodies/` and `$(INST_DIR)/planetary/missions/` — JSON data

### Integrated update workflow

The full rebuild cycle is captured in `$HOME/dev/update.sh`:

```bash
# GRASS core
cd $HOME/dev/grass && make clean && git pull
./configure --without-pdal --with-openmp && make -j8

# Community addons
cd $HOME/dev/grass-addons
make MODULE_TOPDIR=$HOME/dev/grass clean && git pull
make MODULE_TOPDIR=$HOME/dev/grass -j8

# Planetary  (uses MODULE_TOPDIR / INST_DIR from config.mk)
cd $HOME/dev/Planetary
make clean && make -j8

# System install (picks up GRASS + addons + Planetary in one shot)
cd $HOME/dev/grass && sudo make install

# Install Planetary extras (libs, data)
cd $HOME/dev/Planetary && sudo make install
```

---

## Dependencies

### `planetary-cspice` runtime
| Package | Purpose |
|---------|---------|
| `libc6` | C runtime |

### `grass-planetary-addons` runtime
| Package | Purpose |
|---------|---------|
| `planetary-cspice (>= <version>)` | `libcspice.so` (Pre-Depends) |
| `libc6` | C runtime |
| `libgomp1` | OpenMP for C modules and `p.sunmask` |
| `libxml2` | PDS4 label parsing |
| `ocl-icd-libopencl1` | OpenCL ICD dispatcher |
| `python3` | Python script modules |
| `python3-numpy` | Numerical operations in landing pipeline |

### Optional runtime
| Package | Purpose |
|---------|---------|
| `python3-wxpython4 \| python3-wx` | `g.gui.landing` wxPython wizard |
| `python3-pyqt6` | `p.landing.qt` Qt6 standalone wizard |
| `cspice-kernels` | Pre-fetched NAIF kernel bundles |

### Build-time
| Package | Purpose |
|---------|---------|
| `build-essential` | gcc, make |
| `grass-dev (>= 8.0)` | GRASS headers and build system |
| `libxml2-dev` | PDS4 XML headers |
| `libgdal-dev` | GDAL for `p.horizon.gpu` |
| `debhelper-compat (= 13)` | Debian build helper |
| `ocl-icd-opencl-dev` | OpenCL headers (optional; enables GPU paths) |

OpenCL is auto-detected. Without it, `p.crater.draw`, `p.sunmask`, and
`p.horizon.gpu` fall back to OpenMP-only; `libpsunmask.so` is still built.

---

## NAIF SPICE kernel bundles (optional, recommended for landing pipeline)

```bash
p.in.spice bundle=moon-me download=yes   # LRO-era Moon kernels
p.in.spice bundle=mars   download=yes
```

Or point at an existing meta-kernel:

```bash
p.spice.config metakernel=/path/to/my.mk body=MOON frame=MOON_ME
```

Kernels are cached under `~/.grass8/`.

---

## Body and mission data

After deb install, data lives at:

```bash
p.landing body=/usr/lib/grass/addons/bodies/moon.json \
          mission=/usr/lib/grass/addons/missions/luna27.json ...
```

After direct install (`make install INST_DIR=/usr/local/grass86`):

```bash
p.landing body=/usr/local/grass86/planetary/bodies/moon.json \
          mission=/usr/local/grass86/planetary/missions/luna27.json ...
```

---

## Verifying the installation

```bash
grass /tmp/grass_test/PERMANENT --exec \
    python3 -m unittest -v test_pcrater
```

All 30+ tests should pass. See `README.md` for the full test coverage table.
