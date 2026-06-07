MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.in.pds3

PDS_DIR = $(CURDIR)/../../libs/p_pds
PDS_OBJ = $(PDS_DIR)/p_pds.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(PDS_DIR) -fopenmp
EXTRA_LDFLAGS = -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Build p_pds.o before the module binary
$(PDS_OBJ): $(PDS_DIR)/p_pds.c $(PDS_DIR)/p_pds.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PDS_OBJ)

default: cmd
