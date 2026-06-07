MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.albedo

PLANETARY_LIBS ?= $(CURDIR)/../../libs
PHOTO_DIR = $(PLANETARY_LIBS)/p_photomodel
PHOTO_OBJ = $(PHOTO_DIR)/p_photomodel.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(PHOTO_DIR) -fopenmp
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build library object before the module binary
$(PHOTO_OBJ): $(PHOTO_DIR)/p_photomodel.c $(PHOTO_DIR)/p_photomodel.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PHOTO_OBJ)

default: cmd
