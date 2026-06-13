MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.in.isis

PMETA_DIR = $(CURDIR)/../../libs/p_meta
PMETA_OBJ = $(PMETA_DIR)/p_meta.o

LIBES        = $(RASTERLIB) $(GISLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS = -I$(PMETA_DIR)

include $(MODULE_TOPDIR)/include/Make/Module.make

$(PMETA_OBJ): $(PMETA_DIR)/p_meta.c $(PMETA_DIR)/p_meta.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN)/$(PGM)$(EXE): $(PMETA_OBJ)

default: cmd
