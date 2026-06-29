MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.coregister

LIBES        = $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = $(shell pkg-config --cflags fftw3) -O3
EXTRA_LDFLAGS = $(shell pkg-config --libs fftw3) -lm

include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
