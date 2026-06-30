MODULE_TOPDIR ?= $(HOME)/dev/grass
PGM = p.stereo
SCRIPTS = $(PGM)

ASP_SRC     ?= $(HOME)/dev/StereoPipeline
ASP_GIT      = https://github.com/NeoGeographyToolkit/StereoPipeline

# Clone ASP source tree if not already present.
# The binaries are built separately via conda (see BUILDING.md in the clone).
# p.stereo will also try to download a pre-compiled binary tarball at first run.
$(ASP_SRC):
	@if [ -d "$(ASP_SRC)" ]; then \
	    echo "ASP source tree already present at $(ASP_SRC)."; \
	else \
	    echo "Cloning ASP source tree to $(ASP_SRC) ..."; \
	    git clone --depth=1 $(ASP_GIT) $(ASP_SRC); \
	    echo "Done. Build from source: cd $(ASP_SRC) && cat INSTALLGUIDE.rst"; \
	    echo "Or: p.stereo will auto-download a pre-built binary on first run."; \
	fi
	@touch $(ASP_SRC)

include $(MODULE_TOPDIR)/include/Make/Script.make

# Ensure ASP source tree exists before installing the script
script: $(ASP_SRC)

default: script
