MODULE_TOPDIR ?= $(HOME)/dev/grass

PGM = p.crater

EXTRA_OBJS = planet_db.o crater_eq.o

LIBES        = $(VECTORLIB) $(DBMILIB) $(RASTERLIB) $(GISLIB) $(MATHLIB)
DEPENDENCIES = $(VECTORDEP) $(DBMIDEP) $(RASTERDEP) $(GISDEP)

EXTRA_CFLAGS  = -fopenmp $(shell gdal-config --cflags 2>/dev/null || echo -I/usr/include/gdal)
EXTRA_LDFLAGS = -lm -fopenmp

include $(MODULE_TOPDIR)/include/Make/Module.make

# Pre-compile helper objects inside this module dir
$(OBJDIR)/planet_db.o: planet_db.c planet_db.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR)/crater_eq.o: crater_eq.c crater_eq.h planet_db.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR)/impactor_points.o: impactor_points.c impactor_points.h \
                              crater_eq.h planet_db.h | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(BIN)/$(PGM)$(EXE): $(OBJDIR)/planet_db.o \
                      $(OBJDIR)/crater_eq.o \
                      $(OBJDIR)/impactor_points.o

default: cmd
