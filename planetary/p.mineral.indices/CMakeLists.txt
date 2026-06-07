cmake_minimum_required(VERSION 3.22)
project(p.mineral.indices)
include(build_addon OPTIONAL)
find_M()
set(PLANETARY_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
build_addon(${PROJECT_NAME}
  SOURCES main.c "${PLANETARY_LIBS}/p_spectra/p_spectra.c"
  GRASSLIBS gis raster
  DEPENDS LIBM
  DOCFILES p.mineral.indices.html)
target_include_directories(${PROJECT_NAME} PRIVATE "${PLANETARY_LIBS}/p_spectra")
