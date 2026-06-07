build_module(NAME p.crater.draw
             SOURCES main.c detect_dem.c detect_image.c detect_ml.c
                     opencl_runtime.c cl_kernels.c nms_output.c refine.c
             DEPENDS grass_vector grass_dbmi grass_raster grass_gis)
