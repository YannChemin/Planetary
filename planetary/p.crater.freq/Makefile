MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.crater.freq

LIBES        = $(VECTORLIB) $(DBMILIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(VECTORDEP) $(DBMIDEP) $(GISDEP)

EXTRA_CFLAGS  = $(shell gdal-config --cflags 2>/dev/null || echo -I/usr/include/gdal)

include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
