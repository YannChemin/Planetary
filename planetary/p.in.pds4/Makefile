MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.in.pds4

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

# libxml2 for PDS4 XML parsing
EXTRA_CFLAGS  = $(shell xml2-config --cflags)
EXTRA_LDFLAGS = $(shell xml2-config --libs)

include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
