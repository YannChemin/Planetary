MODULE_TOPDIR ?= $(HOME)/dev/grass

# All subdirectories that contain a Makefile are modules.
# p.landing.qt has no Makefile (Qt6 standalone wizard, not a GRASS module).
SUBDIRS := $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))

include $(MODULE_TOPDIR)/include/Make/Dir.make

# After building all modules, copy the shared Python libraries into the dist
# tree so that 'sudo make install' from the GRASS source dir carries them to
# /usr/local/grass86/ alongside the scripts.  RUN_GISBASE is the dist tree
# path defined by Platform.make (e.g. .../dist.x86_64-pc-linux-gnu).
default: subdirs
	install -m 0644 p_lib.py   $(RUN_GISBASE)/p_lib.py
	install -m 0644 p_spice.py $(RUN_GISBASE)/p_spice.py

install: installsubdirs
	install -m 0644 p_lib.py   $(INST_DIR)/p_lib.py
	install -m 0644 p_spice.py $(INST_DIR)/p_spice.py
