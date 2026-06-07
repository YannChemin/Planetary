## DESCRIPTION

**g.gui.landing** launches the Planetary Landing Site Evaluation Wizard
inside the GRASS wxGUI. It appears in the GRASS menu bar under
**Planetary → Evaluation wizard** when the toolbox XML files are installed.

The wizard guides the user through all stages of the `p.*` pipeline via
wxPython wizard pages. GRASS modules execute in a background thread so the
GUI remains responsive during long computations (e.g., illumination
simulation). The Next button on each page is locked until the step succeeds.

### Wizard pages

| Page | Modules | Description |
|------|---------|-------------|
| 1. Setup | — | Select body JSON, mission JSON, DEM raster, output prefix |
| 2. Import | p.in.pds, p.in.dem, p.in.ancillary | Import PDS/ISIS3 DEMs and ancillary data |
| 3. Terrain | p.terrain.* | Multi-scale slope, roughness, hazard, ellipse scan |
| 4. Illumination | p.illumination.* | Solar illumination fraction and shadow frequency |
| 5. Visibility | p.visibility.* | Earth visibility, horizon masking, orbiter contact |
| 6. MCDM | p.mcdm.* | AHP weight elicitation and WLC+TOPSIS scoring |
| 7. Rank | p.rank | Candidate ranking with Monte Carlo sensitivity table |

### GRASS wxGUI toolbox registration

The Planetary menu is registered via two XML files in `~/.grass8/toolboxes/`:
- `toolboxes.xml` — defines the Planetary top-level menu and all p.* subtoolboxes
- `main_menu.xml` — links the Planetary toolbox into the menu bar

Both files are installed automatically by the `p-landing-grass` Debian package.

## NOTES

Requires wxPython 4.x (`python3-wxgtk4.0`) and must be run inside a GRASS
session. For a standalone Qt6 wizard outside the GRASS GUI, use `p-landing-qt`.

## EXAMPLES

```bash
# Launch from the GRASS terminal
g.gui.landing

# Or use the Planetary menu in the GRASS wxGUI:
# Planetary → Evaluation wizard → Landing-site evaluation wizard (wxPython)
```

## SEE ALSO

*[p.landing](p.landing.md),
[p.terrain.slope](p.terrain.slope.md),
[p.illumination.sunfraction](p.illumination.sunfraction.md),
[p.rank](p.rank.md)*

## REFERENCES

- Liu, H. et al. (2023) A New Blind Selection Approach for Lunar Landing
  Zones Based on Engineering Constraints Using Sliding Window.
  *Remote Sensing* 15, 3184. doi:10.3390/rs15123184
- Turchinskaya, O.I. & Slyuta, E.N. (2024) Landing site choice for Luna-27
  mission in the Moon South Polar Region. *Acta Astronautica* 222, 346–358.
  doi:10.1016/j.actaastro.2024.06.011
- Golombek, M.P. et al. (2003) Selection of the Mars Exploration Rover
  landing sites. *Journal of Geophysical Research: Planets* 108(E12), 8072.
  doi:10.1029/2003JE002074
- Saaty, T.L. (1977) A scaling method for priorities in hierarchical
  structures. *Journal of Mathematical Psychology* 15(3), 234–281.
  doi:10.1016/0022-2496(77)90033-5

## AUTHOR

Yann Chemin
