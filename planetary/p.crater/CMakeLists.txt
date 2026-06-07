build_module(NAME p.crater
             SOURCES main.c planet_db.c crater_eq.c impactor_points.c
             DEPENDS grass_vector grass_dbmi grass_raster grass_gis)
