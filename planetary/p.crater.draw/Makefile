MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.crater.draw

LIBES        = $(VECTORLIB) $(DBMILIB) $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(VECTORDEP) $(DBMIDEP) $(RASTERDEP) $(GISDEP)

# Detect OpenCL: enable -DHAVE_OPENCL and link -lOpenCL only when both
# <CL/cl.h> and libOpenCL.so are visible. Detector code paths fall back
# to OpenMP transparently when HAVE_OPENCL is undefined.
HAVE_CL := $(shell test -f /usr/include/CL/cl.h && echo 1)
ifeq ($(HAVE_CL),1)
  OPENCL_CFLAGS  = -DHAVE_OPENCL
  OPENCL_LDFLAGS = -lOpenCL
else
  OPENCL_CFLAGS  =
  OPENCL_LDFLAGS =
endif

EXTRA_CFLAGS  = -fopenmp $(OPENCL_CFLAGS) $(shell gdal-config --cflags 2>/dev/null || echo -I/usr/include/gdal)
EXTRA_LDFLAGS = -lm -fopenmp $(OPENCL_LDFLAGS)

include $(MODULE_TOPDIR)/include/Make/Module.make

$(OBJDIR)/detect_dem.o:   detect_dem.c   p_crater_draw.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/detect_image.o: detect_image.c p_crater_draw.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/nms_output.o:   nms_output.c   p_crater_draw.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/detect_ml.o:    detect_ml.c    p_crater_draw.h detect_ml.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/opencl_runtime.o: opencl_runtime.c opencl_runtime.h cl_kernels.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/cl_kernels.o:    cl_kernels.c    cl_kernels.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@
$(OBJDIR)/refine.o:       refine.c       refine.h p_crater_draw.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(BIN)/$(PGM)$(EXE): $(OBJDIR)/detect_dem.o \
                      $(OBJDIR)/detect_image.o \
                      $(OBJDIR)/nms_output.o \
                      $(OBJDIR)/detect_ml.o \
                      $(OBJDIR)/opencl_runtime.o \
                      $(OBJDIR)/cl_kernels.o \
                      $(OBJDIR)/refine.o

default: cmd
