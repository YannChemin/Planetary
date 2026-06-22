MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.phocube

PLANETARY_LIBS ?= $(CURDIR)/../../libs
SHAPE_DIR  = $(PLANETARY_LIBS)/p_shapemodel
SHAPE_OBJ  = $(SHAPE_DIR)/p_shapemodel.o
SPICE_DIR  ?= $(PLANETARY_LIBS)/p_spice
CSPICE_DIR ?= $(HOME)/dev/cspice
SPICE_OBJ   = $(SPICE_DIR)/p_spice.o

GDAL_CFLAGS := $(shell pkg-config --cflags gdal 2>/dev/null || gdal-config --cflags 2>/dev/null)
GDAL_LIBS   := $(shell pkg-config --libs   gdal 2>/dev/null || gdal-config --libs   2>/dev/null)

LIBES        = $(RASTERLIB) $(GPROJLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GPROJDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(SHAPE_DIR) -I$(SPICE_DIR) -I$(CSPICE_DIR)/include $(GDAL_CFLAGS) -fopenmp
EXTRA_LDFLAGS = $(CSPICE_DIR)/lib/cspice.a $(GDAL_LIBS) -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library objects before the module binary
$(SHAPE_OBJ): $(SHAPE_DIR)/p_shapemodel.c $(SHAPE_DIR)/p_shapemodel.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(SPICE_OBJ): $(SPICE_DIR)/p_spice.c $(SPICE_DIR)/p_spice.h
	$(CC) $(ALL_CFLAGS) -I$(CSPICE_DIR)/include -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(SHAPE_OBJ) $(SPICE_OBJ)

default: cmd
