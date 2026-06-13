#!/usr/bin/make -f
SHELL := /bin/bash

# Local path overrides — copy config.mk.example to config.mk and edit.
-include config.mk

# Point at the GRASS GIS source tree.
# Override in config.mk or on the command line: make MODULE_TOPDIR=/path/to/grass
MODULE_TOPDIR ?= $(HOME)/dev/grass

# Derived GRASS paths (mirrors grass-addons convention)
GRASS_PREFIX := $(shell ls -d /usr/local/grass[0-9]* 2>/dev/null | sort -V | tail -1)
ifeq ($(GRASS_PREFIX),)
  GRASS_PREFIX := /usr/local/grass86
endif

# Install target: default is the running GRASS installation.
# For user-level: make install INST_DIR=~/.grass8/addons
INST_DIR ?= $(GRASS_PREFIX)

CSPICE_SRC   := $(CURDIR)/cspice-pkg/cspice
CSPICE_BUILD := $(CURDIR)/cspice-pkg/build

# GRASS toolboxes.py has a Python 3 bug: write_text(bytes) instead of write_bytes.
# This prevents user toolboxes (Planetary menu) from ever being written to disk.
# We patch the GRASS source before building and restore it afterwards.
GRASS_TOOLBOXES := $(MODULE_TOPDIR)/gui/wxpython/core/toolboxes.py
GRASS_TOOLBOXES_BAK := $(GRASS_TOOLBOXES).planetary.bak

.PHONY: all cspice utils libs modules install clean clean-obj deb patch-grass unpatch-grass

# ── default ───────────────────────────────────────────────────────────────────
all: cspice utils modules

# ── libcspice.so ──────────────────────────────────────────────────────────────
cspice: $(CSPICE_BUILD)/libcspice.so

$(CSPICE_BUILD)/libcspice.so:
	mkdir -p $(CSPICE_BUILD)
	gcc -shared -fPIC -O2 -w -std=gnu89 -DNON_UNIX_STDIO \
	    -I$(CSPICE_SRC)/include \
	    -o $@ \
	    $(CSPICE_SRC)/src/cspice/*.c -lm
	@echo "=== libcspice.so built ==="

# ── SPICE utility programs ────────────────────────────────────────────────────
utils: $(CSPICE_BUILD)/libcspice.so
	$(MAKE) -C cspice-pkg/utils CSPICE_BUILD=$(CSPICE_BUILD) INST_DIR=$(INST_DIR)

# ── GRASS toolboxes.py patch (apply before build, always restore after) ──────
patch-grass:
	@if grep -q 'write_text(xml)' $(GRASS_TOOLBOXES) 2>/dev/null; then \
	    cp $(GRASS_TOOLBOXES) $(GRASS_TOOLBOXES_BAK); \
	    sed -i 's/Path(menudataFile)\.write_text(xml)/Path(menudataFile).write_bytes(xml)/' \
	        $(GRASS_TOOLBOXES); \
	    echo "=== Patched $(GRASS_TOOLBOXES) ==="; \
	fi

unpatch-grass:
	@if [ -f $(GRASS_TOOLBOXES_BAK) ]; then \
	    mv $(GRASS_TOOLBOXES_BAK) $(GRASS_TOOLBOXES); \
	    echo "=== Restored $(GRASS_TOOLBOXES) ==="; \
	fi

# ── all modules via GRASS build system ───────────────────────────────────────
# libs/ sub-objects are compiled on demand by each module's DEPENDENCIES.
# Removes stale OBJ.* dirs first to prevent GRASS version hash mismatch.
# The patch/unpatch wrapper ensures toolboxes.py is always restored even if
# the build fails.
modules: clean-obj
	python3 scripts/md2html_grass.py planetary/
	@$(MAKE) patch-grass
	@_exit=0; \
	$(MAKE) -C planetary MODULE_TOPDIR=$(MODULE_TOPDIR) || _exit=$$?; \
	$(MAKE) unpatch-grass; \
	exit $$_exit

# ── libpsunmask.so (ctypes library used by p.illumination.sunfraction) ────────
libpsunmask:
	$(MAKE) -C planetary/p.sunmask -f Makefile.standalone \
	    GISBASE=$(GRASS_PREFIX)

# ── install directly into GRASS (mirrors update.sh workflow) ─────────────────
# Modules land in $(INST_DIR)/bin/ and $(INST_DIR)/scripts/.
# p_lib.py and p_spice.py go to $(INST_DIR)/ so that
# dirname(dirname(abspath(script))) resolves correctly for scripts/.
# Build first with 'make' or 'make all', then 'sudo make install'.
install:
	$(MAKE) -C cspice-pkg/utils CSPICE_BUILD=$(CSPICE_BUILD) INST_DIR=$(INST_DIR) install
	$(MAKE) -C planetary MODULE_TOPDIR=$(MODULE_TOPDIR) \
	    INST_DIR=$(INST_DIR) install
	mkdir -p $(INST_DIR)/planetary
	for f in bodies/moon.json bodies/mars.json bodies/venus.json; do \
	    install -m 0644 $$f $(INST_DIR)/planetary/$$(basename $$f); \
	done
	mkdir -p $(INST_DIR)/planetary/missions
	for f in missions/*.json missions/README.md; do \
	    [ -f "$$f" ] && install -m 0644 $$f \
	        $(INST_DIR)/planetary/missions/$$(basename $$f) || true; \
	done
	# ── wxGUI toolboxes → user config dir ─────────────────────────────────
	@TBDIR="$(HOME)/.grass8/toolboxes"; \
	mkdir -p "$$TBDIR"; \
	for f in toolboxes/main_menu.xml toolboxes/toolboxes.xml; do \
	    dest="$$TBDIR/$$(basename $$f)"; \
	    if [ -f "$$dest" ]; then \
	        echo "  Backing up $$dest → $$dest.bak"; \
	        cp "$$dest" "$$dest.bak"; \
	    fi; \
	    install -m 0644 $$f "$$dest"; \
	    echo "  Installed $$f → $$dest"; \
	done
	@echo "=== Installed to $(INST_DIR) ==="

# ── Debian package build ──────────────────────────────────────────────────────
# Stamps a datetime version suffix so dpkg always overwrites installed files.
deb: clean
	python3 debian/stamp-version.py
	dpkg-buildpackage -us -uc -b

# ── remove stale OBJ dirs (prevents GRASS version hash mismatch) ─────────────
clean-obj:
	@find planetary -maxdepth 2 -name 'OBJ.*' -type d -exec rm -rf {} + 2>/dev/null || true

# ── full clean ────────────────────────────────────────────────────────────────
clean:
	rm -f $(CSPICE_BUILD)/libcspice.so
	$(MAKE) -C cspice-pkg/utils clean 2>/dev/null || true
	find planetary -maxdepth 2 -name 'OBJ.*' -type d -exec rm -rf {} + 2>/dev/null || true
	$(MAKE) -C planetary MODULE_TOPDIR=$(MODULE_TOPDIR) clean 2>/dev/null || true
	$(MAKE) -C planetary/p.sunmask -f Makefile.standalone clean 2>/dev/null || true
	$(MAKE) -C planetary/p.horizon.gpu -f Makefile.standalone clean 2>/dev/null || true
	find planetary -name '__pycache__' -exec rm -rf {} + 2>/dev/null || true
	find planetary -name '*.pyc' -delete 2>/dev/null || true
	find planetary -maxdepth 3 -name '*.html' -delete 2>/dev/null || true
	find planetary -name '.pytest_cache' -exec rm -rf {} + 2>/dev/null || true
	rm -rf debian/.debhelper debian/debhelper-build-stamp \
	       debian/grass-planetary-addons debian/planetary-cspice \
	       debian/*.substvars debian/*.debhelper.log debian/files
	@echo "=== Clean done ==="
