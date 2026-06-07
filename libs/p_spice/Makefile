MODULE_TOPDIR = ../../..

LIB_NAME = p_spice

CSPICE_DIR = $(HOME)/dev/cspice

LIBES        = $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(GISDEP)

EXTRA_CFLAGS  = -I$(GRASS_INC) -I$(CSPICE_DIR)/include -fopenmp
EXTRA_LDFLAGS = -L$(CSPICE_DIR)/lib -lcspice -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Lib.make

default: lib
