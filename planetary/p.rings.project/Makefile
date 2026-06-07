MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.rings.project

PLANETARY_LIBS ?= $(CURDIR)/../../libs
PROJ_DIR = $(PLANETARY_LIBS)/p_projection_planet
PROJ_OBJ = $(PROJ_DIR)/p_projection_planet.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(PROJ_DIR) -fopenmp
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library object before the module binary
$(PROJ_OBJ): $(PROJ_DIR)/p_projection_planet.c $(PROJ_DIR)/p_projection_planet.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PROJ_OBJ)

default: cmd
