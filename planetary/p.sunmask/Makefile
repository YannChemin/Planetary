MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.sunmask

# ── required GRASS libraries ──────────────────────────────────────────────
LIBES = $(GISLIB) $(RASTERLIB)

# ── always compile with OpenMP ────────────────────────────────────────────
EXTRA_CFLAGS  = -fopenmp -O2
EXTRA_LDFLAGS = -fopenmp -lm

# ── optional OpenCL ───────────────────────────────────────────────────────
# Try libOpenCL via pkg-config, then fall back to direct -lOpenCL.
# If neither works the build continues without GPU support.

OCL_PKG_CFLAGS := $(shell pkg-config --cflags OpenCL 2>/dev/null)
OCL_PKG_LIBS   := $(shell pkg-config --libs   OpenCL 2>/dev/null)

ifeq ($(OCL_PKG_LIBS),)
    # pkg-config didn't find it; try the ICD loader directly
    OCL_DIRECT := $(shell echo "int main(){}" | \
        gcc -x c - -lOpenCL -o /dev/null 2>/dev/null && echo yes || echo no)
    ifeq ($(OCL_DIRECT),yes)
        OCL_PKG_LIBS   = -lOpenCL
        OCL_PKG_CFLAGS =
    endif
endif

ifneq ($(OCL_PKG_LIBS),)
    EXTRA_CFLAGS  += -DHAVE_OPENCL $(OCL_PKG_CFLAGS)
    EXTRA_LDFLAGS += $(OCL_PKG_LIBS)
    $(info p.sunmask: OpenCL enabled ($(OCL_PKG_LIBS)))
else
    $(info p.sunmask: OpenCL NOT found — building OpenMP-only)
endif

# ── GRASS build rules ─────────────────────────────────────────────────────
include $(MODULE_TOPDIR)/include/Make/Module.make

default: cmd
