MODULE_TOPDIR ?= $(HOME)/dev/grass

# All subdirectories that contain a Makefile are modules.
# p.landing.qt has no Makefile (Qt6 standalone wizard, not a GRASS module).
SUBDIRS := $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))

include $(MODULE_TOPDIR)/include/Make/Dir.make

default: subdirs

install: installsubdirs
