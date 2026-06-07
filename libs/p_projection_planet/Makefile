MODULE_TOPDIR = ../../..

LIB_NAME = p_projection_planet

LIBES        = $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(GISDEP)

EXTRA_CFLAGS  = -I$(GRASS_INC) -fopenmp
EXTRA_LDFLAGS = -fopenmp

include $(MODULE_TOPDIR)/include/Make/Lib.make

default: lib
