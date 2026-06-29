MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.spec.pca

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -fopenmp
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
