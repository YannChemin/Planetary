MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.atcorr.hapke

PLANETARY_LIBS ?= $(CURDIR)/../../libs
ATMOS_DIR = $(PLANETARY_LIBS)/p_atmosmodel
ATMOS_OBJ = $(ATMOS_DIR)/p_atmosmodel.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(ATMOS_DIR) -fopenmp
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library object before the module binary
$(ATMOS_OBJ): $(ATMOS_DIR)/p_atmosmodel.c $(ATMOS_DIR)/p_atmosmodel.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(ATMOS_OBJ)

default: cmd
