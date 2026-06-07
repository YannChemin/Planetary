MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.shadow.planet

PLANETARY_LIBS ?= $(CURDIR)/../../libs
SHAPE_DIR = $(PLANETARY_LIBS)/p_shapemodel
SHAPE_OBJ = $(SHAPE_DIR)/p_shapemodel.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(SHAPE_DIR) -fopenmp
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library object before the module binary
$(SHAPE_OBJ): $(SHAPE_DIR)/p_shapemodel.c $(SHAPE_DIR)/p_shapemodel.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(SHAPE_OBJ)

default: cmd
