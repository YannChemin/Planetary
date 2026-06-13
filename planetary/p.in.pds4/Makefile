MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.in.pds4

PMETA_DIR = $(CURDIR)/../../libs/p_meta
PMETA_OBJ = $(PMETA_DIR)/p_meta.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

# libxml2 for PDS4 XML parsing
EXTRA_CFLAGS  = $(shell xml2-config --cflags) -I$(PMETA_DIR)
EXTRA_LDFLAGS = $(shell xml2-config --libs)

include $(MODULE_TOPDIR)/include/Make/Module.make

$(PMETA_OBJ): $(PMETA_DIR)/p_meta.c $(PMETA_DIR)/p_meta.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PMETA_OBJ)

default: cmd
