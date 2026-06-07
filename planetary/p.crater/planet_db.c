/****************************************************************************
 *
 * MODULE:       p.crater (planet_db.c)
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Planetary body database for impact-crater scaling.
 *
 *               Provides per-body defaults for gravity, mean radius,
 *               bulk and surface density, and dominant Gault target type.
 *
 *               Density values follow:
 *               - Moon regolith:  Carrier 1973, Heiken et al. 1991 (~1500 kg/m^3)
 *               - Mars surface:   Smith et al. 1999 (regolith ~1500), basaltic
 *                                 crust ~2900 kg/m^3 (Wieczorek & Zuber 2004)
 *               - Mercury:        Padovan et al. 2015 (crustal ~3000),
 *                                 Phillips et al. 2018 (regolith ~1800)
 *               - Venus:          Konopliv et al. 1999 (basaltic ~2900)
 *               - Earth:          continental crust ~2700 kg/m^3
 *               - Ceres:          Park et al. 2016 (silicate/ice mix ~1600)
 *               - Vesta:          Russell et al. 2012, Konopliv et al. 2014 (~2900)
 *               - Europa/Ganymede/Callisto/Titan: water ice surface ~920
 *               - Io:             silicate/sulfur ~2900
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include "planet_db.h"

/* Per-body simple-crater depth/diameter ratios.
 * Sources:
 *   Moon       0.196   Pike (1977) lunar measurements (canonical reference)
 *   Mars       0.150   Pike (1980); softer impact volatiles -> shallower
 *   Mercury    0.180   Pike (1988)
 *   Venus      0.140   Schaber et al. (1992); atmospheric blanketing
 *   Earth      0.130   Grieve & Pesonen (1992); strong gravitational/erosional
 *                      modification
 *   Vesta      0.180   Marchi et al. (2012); HED parent body
 *   Ceres      0.170   Hiesinger et al. (2016) Dawn data
 *   Icy moons  0.150   water-ice rheology yields shallower craters than rock;
 *                      used for Europa/Ganymede/Callisto/Titan/Saturnian +
 *                      Uranian moons / TNOs / Triton / Pluto / Charon
 *   Small bodies (Phobos, Deimos, asteroids, 67P): 0.200 - small rubble piles
 *                      tend to have steep-walled bowl-shaped craters
 *   custom:    0.196 default, can be overridden via future option
 */

/* Measured simple-to-complex transition diameters Dsc [km]:
 *   Moon       18.0   Pike (1977)
 *   Mars        7.0   Pike (1980)
 *   Mercury    10.3   Pike (1988)
 *   Venus      14.0   Schaber et al. (1992)
 *   Earth       3.2   Pike (1980), continental crust
 *   Ceres      10.0   Hiesinger et al. (2016)
 *   Vesta       7.0   Marchi et al. (2012)
 *   Ganymede    3.5   Schenk (2002) - water-ice rheology lowers Dsc
 *   Callisto    3.5   Schenk (2002)
 *   Europa      4.0   Schenk (2002)
 *   Titan       3.0   Wood et al. (2010) Cassini RADAR
 *   Io          5.0   estimated from g and silicate analogy (no measured)
 *   Mid-sized icy moons (Mimas..Oberon, Triton): use 0.0 -> 1/g fallback
 *   Small bodies (<300 km): all impacts are strength-controlled, no
 *                            simple/complex transition; 0.0 = fallback
 *                            yields huge Dsc, so everything stays simple.
 */

static const PCraterBody BODIES[] = {
    /* ---- Major terrestrial bodies ---- */
    /* name       g        R_km     bulk_rho  surf_rho  tt   dD     Dsc_km  description                       */
    { "mercury",  3.701,   2439.7,  5427.0,   1800.0,   3,   0.180, 10.3,   "Mercury (silicate regolith)"     },
    { "venus",    8.870,   6051.8,  5243.0,   2900.0,   3,   0.140, 14.0,   "Venus (basaltic surface)"        },
    { "earth",    9.807,   6371.0,  5514.0,   2700.0,   3,   0.130,  3.2,   "Earth (continental crust)"       },
    { "mars",     3.711,   3389.5,  3933.0,   2900.0,   3,   0.150,  7.0,   "Mars (basaltic surface default)" },
    { "moon",     1.622,   1737.4,  3344.0,   1500.0,   2,   0.196, 18.0,   "Earth's Moon (Luna)"             },

    /* ---- Galilean moons (Jupiter) ---- */
    { "io",       1.796,   1821.6,  3528.0,   2900.0,   3,   0.150,  5.0,   "Io (silicate/sulfur)"            },
    { "europa",   1.315,   1560.8,  3013.0,    920.0,   1,   0.150,  4.0,   "Europa (water ice shell)"        },
    { "ganymede", 1.428,   2634.1,  1936.0,    920.0,   1,   0.150,  3.5,   "Ganymede (water ice surface)"    },
    { "callisto", 1.235,   2410.3,  1834.0,    920.0,   1,   0.150,  3.5,   "Callisto (water ice/rock mix)"   },

    /* ---- Saturnian moons ---- */
    { "titan",    1.352,   2574.7,  1882.0,    920.0,   1,   0.150,  3.0,   "Titan (water ice + organics)"    },
    { "mimas",    0.0648,   198.2,  1148.0,    920.0,   1,   0.150,  0.0,   "Mimas (Saturn, water ice)"       },
    { "enceladus",0.113,    252.1,  1609.0,    920.0,   1,   0.150,  0.0,   "Enceladus (water ice + active)"  },
    { "tethys",   0.146,    531.0,   984.0,    920.0,   1,   0.150,  0.0,   "Tethys (pure water ice)"         },
    { "dione",    0.232,    561.4,  1478.0,    920.0,   1,   0.150,  0.0,   "Dione (rock + water ice)"        },
    { "rhea",     0.264,    763.8,  1236.0,    920.0,   1,   0.150,  0.0,   "Rhea (water ice + silicates)"    },
    { "iapetus",  0.223,    734.5,  1088.0,    920.0,   1,   0.150,  0.0,   "Iapetus (dark/bright ice)"       },
    { "hyperion", 0.0179,   135.0,   544.0,    700.0,   1,   0.180,  0.0,   "Hyperion (porous water ice)"     },
    { "phoebe",   0.0394,   106.5,  1638.0,    920.0,   1,   0.150,  0.0,   "Phoebe (captured centaur)"       },

    /* ---- Uranian moons ---- */
    { "miranda",  0.079,    235.8,  1200.0,    920.0,   1,   0.150,  0.0,   "Miranda (Uranus, water ice)"     },
    { "ariel",    0.249,    578.9,  1592.0,    920.0,   1,   0.150,  0.0,   "Ariel (Uranus, water ice)"       },
    { "umbriel",  0.230,    584.7,  1392.0,    920.0,   1,   0.150,  0.0,   "Umbriel (Uranus, dark ice)"      },
    { "titania",  0.371,    788.4,  1711.0,    920.0,   1,   0.150,  0.0,   "Titania (Uranus, water ice)"     },
    { "oberon",   0.346,    761.4,  1630.0,    920.0,   1,   0.150,  0.0,   "Oberon (Uranus, water ice)"      },

    /* ---- Neptunian moon ---- */
    { "triton",   0.779,   1353.4,  2061.0,    920.0,   1,   0.150,  0.0,   "Triton (N2/CH4 ice surface)"     },

    /* ---- Martian moons ---- */
    { "phobos",   0.0057,    11.27, 1872.0,   1500.0,   2,   0.200,  0.0,   "Phobos (Mars moon, rubble pile)" },
    { "deimos",   0.0030,     6.20, 1471.0,   1500.0,   2,   0.200,  0.0,   "Deimos (Mars moon)"              },

    /* ---- IAU-recognized dwarf planets ---- */
    { "pluto",    0.620,   1188.3,  1854.0,    920.0,   1,   0.150,  0.0,   "Pluto (N2/CH4/H2O ice surface)"  },
    { "charon",   0.288,    606.0,  1702.0,    920.0,   1,   0.150,  0.0,   "Charon (Pluto's large moon)"     },
    { "ceres",    0.284,    469.7,  2161.0,   1600.0,   2,   0.170, 10.0,   "Ceres (silicate-ice mix)"        },
    { "eris",     0.824,   1163.0,  2430.0,    920.0,   1,   0.150,  0.0,   "Eris (N2/CH4 ice surface)"       },
    { "haumea",   0.401,    780.0,  1885.0,    920.0,   1,   0.150,  0.0,   "Haumea (water ice surface)"      },
    { "makemake", 0.500,    715.0,  2100.0,    920.0,   1,   0.150,  0.0,   "Makemake (CH4 ice surface)"      },

    /* ---- Candidate dwarf planets / large TNOs ---- */
    { "gonggong", 0.220,    615.0,  1740.0,    920.0,   1,   0.150,  0.0,   "Gonggong (225088 - TNO)"         },
    { "quaoar",   0.276,    555.0,  2000.0,    920.0,   1,   0.150,  0.0,   "Quaoar (50000 - TNO)"            },
    { "sedna",    0.330,    498.0,  2000.0,    920.0,   1,   0.150,  0.0,   "Sedna (90377 - distant TNO)"     },
    { "orcus",    0.250,    458.0,  1530.0,    920.0,   1,   0.150,  0.0,   "Orcus (90482 - plutino)"         },
    { "salacia",  0.250,    423.0,  1500.0,    920.0,   1,   0.150,  0.0,   "Salacia (120347 - TNO)"          },

    /* ---- Notable asteroids ---- */
    { "vesta",    0.220,    262.7,  3456.0,   2900.0,   3,   0.180,  7.0,   "Vesta (HED meteorite parent)"    },
    { "pallas",   0.205,    256.0,  2890.0,   2700.0,   3,   0.200,  0.0,   "Pallas (B-type asteroid)"        },
    { "hygiea",   0.143,    216.5,  1944.0,   2700.0,   3,   0.200,  0.0,   "Hygiea (C-type, dwarf planet?)"  },
    { "psyche",   0.144,    113.0,  3880.0,   3500.0,   3,   0.200,  0.0,   "Psyche (M-type, metallic)"       },
    { "lutetia",  0.044,     49.0,  3400.0,   2700.0,   3,   0.200,  0.0,   "Lutetia (M-type, Rosetta)"       },
    { "mathilde", 0.010,     26.4,  1300.0,   1300.0,   2,   0.200,  0.0,   "Mathilde (C-type, porous)"       },
    { "eros",     0.006,      8.42, 2670.0,   2700.0,   3,   0.200,  0.0,   "Eros (433, NEAR mission)"        },
    { "itokawa",  0.00009,    0.165,1900.0,   1900.0,   2,   0.200,  0.0,   "Itokawa (25143, rubble pile)"    },
    { "bennu",    0.00006,    0.245,1190.0,   1190.0,   2,   0.200,  0.0,   "Bennu (101955, OSIRIS-REx)"      },
    { "ryugu",    0.00012,    0.448,1190.0,   1190.0,   2,   0.200,  0.0,   "Ryugu (162173, Hayabusa2)"       },

    /* ---- Comets ---- */
    { "67p",      0.0001,     2.0,   533.0,    533.0,   2,   0.200,  0.0,   "67P/Churyumov-Gerasimenko"       },

    /* ---- Custom: user must supply gravity, target_density, target_type ---- */
    { "custom",   0.0,       0.0,   0.0,        0.0,   0,   0.196,  0.0,   "User-defined body (requires gravity, target_density, target_type overrides)" },
};

#define NBODIES (int)(sizeof(BODIES) / sizeof(BODIES[0]))

const PCraterBody *p_crater_body_lookup(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < NBODIES; i++) {
        if (strcasecmp(BODIES[i].name, name) == 0)
            return &BODIES[i];
    }
    return NULL;
}

const char *p_crater_body_options(void)
{
    /* Comma-separated list, built once on first call. */
    static char buf[2048] = {0};
    if (buf[0] == '\0') {
        size_t pos = 0;
        for (int i = 0; i < NBODIES; i++) {
            size_t n = strlen(BODIES[i].name);
            if (pos + n + 2 >= sizeof(buf)) break;
            if (i > 0) buf[pos++] = ',';
            memcpy(buf + pos, BODIES[i].name, n);
            pos += n;
        }
        buf[pos] = '\0';
    }
    return buf;
}

int p_crater_body_count(void)
{
    return NBODIES;
}

const PCraterBody *p_crater_body_at(int index)
{
    if (index < 0 || index >= NBODIES) return NULL;
    return &BODIES[index];
}
