MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.spec.class

PLANETARY_LIBS ?= $(CURDIR)/../../libs
SPECTRA_DIR = $(PLANETARY_LIBS)/p_spectra
SPECTRA_OBJ = $(SPECTRA_DIR)/p_spectra.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(SPECTRA_DIR)
EXTRA_LDFLAGS = -lm

include $(MODULE_TOPDIR)/include/Make/Module.make

$(SPECTRA_OBJ): $(SPECTRA_DIR)/p_spectra.c $(SPECTRA_DIR)/p_spectra.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(SPECTRA_OBJ)

default: cmd
