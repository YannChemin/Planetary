MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.cam2map

PLANETARY_LIBS ?= $(CURDIR)/../../libs
SHAPE_DIR  = $(PLANETARY_LIBS)/p_shapemodel
SHAPE_OBJ  = $(SHAPE_DIR)/p_shapemodel.o
SPICE_DIR  ?= $(PLANETARY_LIBS)/p_spice
CSPICE_DIR ?= $(HOME)/dev/cspice
SPICE_OBJ   = $(SPICE_DIR)/p_spice.o
PMETA_DIR  = $(PLANETARY_LIBS)/p_meta
PMETA_OBJ  = $(PMETA_DIR)/p_meta.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(SHAPE_DIR) -I$(SPICE_DIR) -I$(PMETA_DIR) -I$(CSPICE_DIR)/include -fopenmp
EXTRA_LDFLAGS = $(CSPICE_DIR)/lib/cspice.a -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library objects before the module binary
$(SHAPE_OBJ): $(SHAPE_DIR)/p_shapemodel.c $(SHAPE_DIR)/p_shapemodel.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(SPICE_OBJ): $(SPICE_DIR)/p_spice.c $(SPICE_DIR)/p_spice.h
	$(CC) $(ALL_CFLAGS) -I$(CSPICE_DIR)/include -c $< -o $@

$(PMETA_OBJ): $(PMETA_DIR)/p_meta.c $(PMETA_DIR)/p_meta.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(SHAPE_OBJ) $(SPICE_OBJ) $(PMETA_OBJ)

default: cmd
