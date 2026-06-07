cmake_minimum_required(VERSION 3.22)
project(p.rings.project)
include(build_addon OPTIONAL)
find_M()
set(PLANETARY_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
build_addon(${PROJECT_NAME}
  SOURCES main.c "${PLANETARY_LIBS}/p_projection_planet/p_projection_planet.c"
  GRASSLIBS gis raster
  DEPENDS LIBM
  DOCFILES p.rings.project.html)
target_include_directories(${PROJECT_NAME} PRIVATE "${PLANETARY_LIBS}/p_projection_planet")
