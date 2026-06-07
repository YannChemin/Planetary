MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.spiceinit

PLANETARY_LIBS ?= $(CURDIR)/../../libs
SPICE_DIR      ?= $(PLANETARY_LIBS)/p_spice
CSPICE_DIR     ?= $(HOME)/dev/cspice
SPICE_OBJ       = $(SPICE_DIR)/p_spice.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(SPICE_DIR) -I$(CSPICE_DIR)/include -fopenmp
EXTRA_LDFLAGS = $(CSPICE_DIR)/lib/cspice.a -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build p_spice.o before the module binary
$(SPICE_OBJ): $(SPICE_DIR)/p_spice.c $(SPICE_DIR)/p_spice.h
	$(CC) $(ALL_CFLAGS) -I$(CSPICE_DIR)/include -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(SPICE_OBJ)

default: cmd
