MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.horizon.gpu

GDAL_CFLAGS := $(shell pkg-config --cflags gdal 2>/dev/null || gdal-config --cflags 2>/dev/null)
GDAL_LIBS   := $(shell pkg-config --libs   gdal 2>/dev/null || gdal-config --libs   2>/dev/null)

OCL_PKG_LIBS := $(shell pkg-config --libs OpenCL 2>/dev/null)
ifneq ($(OCL_PKG_LIBS),)
    OCL_CFLAGS := -DHAVE_OPENCL $(shell pkg-config --cflags OpenCL 2>/dev/null)
    OCL_LIBS   := $(OCL_PKG_LIBS)
    $(info p.horizon.gpu: OpenCL enabled (pkg-config))
else
    OCL_DIRECT := $(shell echo "int main(){}" | gcc -x c - -lOpenCL -o /dev/null 2>/dev/null && echo yes || echo no)
    ifeq ($(OCL_DIRECT),yes)
        OCL_CFLAGS := -DHAVE_OPENCL
        OCL_LIBS   := -lOpenCL
        $(info p.horizon.gpu: OpenCL enabled (direct))
    else
        $(info p.horizon.gpu: OpenCL NOT found — building OpenMP-only)
    endif
endif

LIBES        = $(RASTERLIB) $(GISLIB) $(GPROJLIB) $(MATHLIB)
DEPENDENCIES = $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = $(GDAL_CFLAGS) $(OCL_CFLAGS) -fopenmp -O2
EXTRA_LDFLAGS = $(GDAL_LIBS)   $(OCL_LIBS)   -fopenmp

EXTRA_OBJS =
ifneq ($(OCL_CFLAGS),)
    EXTRA_OBJS = backend_opencl.o
endif

include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
