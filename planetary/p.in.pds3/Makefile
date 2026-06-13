MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.in.pds3

PDS_DIR   = $(CURDIR)/../../libs/p_pds
PDS_OBJ   = $(PDS_DIR)/p_pds.o

PMETA_DIR = $(CURDIR)/../../libs/p_meta
PMETA_OBJ = $(PMETA_DIR)/p_meta.o

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -I$(PDS_DIR) -I$(PMETA_DIR) -fopenmp
EXTRA_LDFLAGS = -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

$(PDS_OBJ): $(PDS_DIR)/p_pds.c $(PDS_DIR)/p_pds.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(PMETA_OBJ): $(PMETA_DIR)/p_meta.c $(PMETA_DIR)/p_meta.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PDS_OBJ) $(PMETA_OBJ)

default: cmd
