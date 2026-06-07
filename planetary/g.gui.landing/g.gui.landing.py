#!/usr/bin/env python3
############################################################################
# MODULE:       g.gui.landing
# PURPOSE:      Launch the Planetary Landing Site Evaluation wizard
#               inside the GRASS wxGUI.  Appears under the
#               "Planetary" category in the GRASS toolbox.
# AUTHOR(S):    Yann Chemin
# LICENSE:      Unlicense (https://unlicense.org)
############################################################################

# %module
# % description: Planetary landing-site evaluation wizard (wxPython).
# % keyword: Planetary
# % keyword: Landing Pipeline
# % keyword: GUI
# % keyword: wizard
# %end

import os
import sys

import grass.script as gs


def main():
    # Locate the wx wizard implementation (sibling module)
    srcdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    wizard_path = os.path.join(srcdir, "g.gui.landing", "landing_wizard_wx.py")

    if not os.path.isfile(wizard_path):
        gs.fatal(f"Wizard script not found at: {wizard_path}")

    # Launch the wizard — it imports wx internally so it only runs when
    # wx is available (i.e. inside a wxGUI session or standalone wx install)
    import importlib.util
    spec = importlib.util.spec_from_file_location("landing_wizard_wx", wizard_path)
    mod  = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.run_wizard()


if __name__ == "__main__":
    options, flags = gs.parser()
    main()
