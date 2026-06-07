##############################################################################
# planetary-cspice — package the NAIF CSPICE toolkit as a shared library
#
# Builds libcspice.so reproducibly from the CSPICE C sources (N0067) and
# stages a Debian package providing the shared object + headers, for use by
# p-landing-grass and other planetary GIS tools.
#
# Targets:
#   build   Compile build/libcspice.so from $(CSPICE_SRC)/src/cspice
#   deb     Build the .deb (depends on build)
#   clean   Remove build artefacts and staged tree
#
# Variables:
#   CSPICE_SRC   CSPICE source tree (default $(HOME)/dev/cspice)
#   VERSION      Package version (default derived from N-number, 0.67.0)
##############################################################################

# CSPICE sources are vendored under cspice/ for a self-contained,
# reproducible build. Override CSPICE_SRC to build from an external tree.
CSPICE_SRC ?= $(CURDIR)/cspice
VERSION    ?= 0.67.0
ARCH       := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

CURDIR_    := $(CURDIR)
BUILD      := $(CURDIR_)/build
PKGROOT    := $(CURDIR_)/packaging
DEB_NAME   := planetary-cspice_$(VERSION)_$(ARCH).deb

# NAIF f2c-generated sources need K&R-era C semantics (gcc >= 14 errors out
# on implicit int otherwise); -ansi/-std=gnu89 matches NAIF's own mkprodct.csh.
CFLAGS     := -shared -fPIC -O2 -w -std=gnu89 -DNON_UNIX_STDIO

.PHONY: all build deb clean

all: build

# ── Compile the shared library from source ──────────────────────────────────
build:
	@if [ ! -d "$(CSPICE_SRC)/src/cspice" ]; then \
	    echo "ERROR: CSPICE sources not found at $(CSPICE_SRC)/src/cspice."; \
	    echo "Set CSPICE_SRC=/path/to/cspice (unpacked NAIF toolkit N0067)."; \
	    exit 1; \
	fi
	@mkdir -p $(BUILD)
	@echo "==> Compiling libcspice.so from $(CSPICE_SRC)/src/cspice (~2229 files)…"
	gcc $(CFLAGS) -I$(CSPICE_SRC)/include \
	    -o $(BUILD)/libcspice.so $(CSPICE_SRC)/src/cspice/*.c -lm
	@echo "==> Built $(BUILD)/libcspice.so"

# ── Build the .deb ───────────────────────────────────────────────────────────
deb: build
	@echo "==> Staging planetary-cspice $(VERSION)"
	rm -rf $(PKGROOT)/usr
	install -d $(PKGROOT)/usr/local/lib
	install -m 0644 $(BUILD)/libcspice.so $(PKGROOT)/usr/local/lib/libcspice.so
	install -d $(PKGROOT)/usr/local/include/cspice
	install -m 0644 $(CSPICE_SRC)/include/*.h $(PKGROOT)/usr/local/include/cspice/
	install -d $(PKGROOT)/usr/local/share/doc/planetary-cspice
	install -m 0644 $(CURDIR_)/DEBIAN/copyright \
	    $(PKGROOT)/usr/local/share/doc/planetary-cspice/copyright

	@# control metadata
	sed -i "s/^Version:.*/Version: $(VERSION)/" $(CURDIR_)/DEBIAN/control
	@INST_SIZE=$$(du -sk $(PKGROOT)/usr | cut -f1); \
	 sed -i "s/^Installed-Size:.*/Installed-Size: $$INST_SIZE/" $(CURDIR_)/DEBIAN/control
	install -d $(PKGROOT)/DEBIAN
	install -m 0644 $(CURDIR_)/DEBIAN/control  $(PKGROOT)/DEBIAN/control
	install -m 0644 $(CURDIR_)/DEBIAN/copyright $(PKGROOT)/DEBIAN/copyright
	install -m 0755 $(CURDIR_)/DEBIAN/postinst $(PKGROOT)/DEBIAN/postinst
	(cd $(PKGROOT) && find usr -type f | sort | xargs md5sum) > $(PKGROOT)/DEBIAN/md5sums

	dpkg-deb --build --root-owner-group $(PKGROOT) $(CURDIR_)/$(DEB_NAME)
	@echo "==> Package ready: $(DEB_NAME)"
	@echo "    Install with:  sudo dpkg -i $(DEB_NAME)"

clean:
	rm -rf $(BUILD) $(PKGROOT)/usr $(PKGROOT)/DEBIAN/md5sums $(CURDIR_)/$(DEB_NAME)
