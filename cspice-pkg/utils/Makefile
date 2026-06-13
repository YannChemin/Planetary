#!/usr/bin/make -f
# Build SPICE utility programs against the Planetary libcspice.so.
# Called from the project root via 'make utils' or 'make install'.

CSPICE_SRC   := $(CURDIR)/../cspice
CSPICE_BUILD := $(CURDIR)/../build

CFLAGS  := -g -O2 -Wall -I$(CSPICE_SRC)/include
LDFLAGS := -L$(CSPICE_BUILD) -lcspice -Wl,-rpath,$(CSPICE_BUILD) -lm

BINS := spice-brief spice-ckbrief spice-chronos spice-commnt

.PHONY: all install clean

all: $(BINS)

spice-brief:   spice-brief.c   $(CSPICE_BUILD)/libcspice.so
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

spice-ckbrief: spice-ckbrief.c $(CSPICE_BUILD)/libcspice.so
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

spice-chronos: spice-chronos.c $(CSPICE_BUILD)/libcspice.so
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

spice-commnt:  spice-commnt.c  $(CSPICE_BUILD)/libcspice.so
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -d $(INST_DIR)/bin
	install -m 0755 $(BINS) $(INST_DIR)/bin/
	@echo "=== SPICE utils installed to $(INST_DIR)/bin/ ==="

clean:
	rm -f $(BINS)
