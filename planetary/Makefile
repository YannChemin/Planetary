MODULE_TOPDIR ?= $(HOME)/dev/grass

# All subdirectories that contain a Makefile are modules.
# p.landing.qt has no Makefile (Qt6 standalone wizard, not a GRASS module).
SUBDIRS := $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))

include $(MODULE_TOPDIR)/include/Make/Dir.make

# p_lib.py and p_spice.py must land in the dist tree BEFORE module html targets
# run: modules like p.terrain.slope import them at module level, so the
# --html-description call (triggered by the html Make target) needs them present.
# Install them first, then build subdirs (which inherits -jN from MAKEFLAGS).
default:
	install -m 0644 p_lib.py   $(RUN_GISBASE)/p_lib.py
	install -m 0644 p_spice.py $(RUN_GISBASE)/p_spice.py
	$(MAKE) subdirs

install: installsubdirs
	install -m 0644 p_lib.py   $(INST_DIR)/p_lib.py
	install -m 0644 p_spice.py $(INST_DIR)/p_spice.py
