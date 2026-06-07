cmake_minimum_required(VERSION 3.22)
project(p.in.pds3)
include(build_addon OPTIONAL)
find_M()

set(PLANETARY_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")

build_addon(
  ${PROJECT_NAME}
  SOURCES
    main.c
    "${PLANETARY_LIBS}/p_pds/p_pds.c"
  GRASSLIBS
    gis
    raster
  DEPENDS
    LIBM
  DOCFILES
    p.in.pds3.html
    p.in.pds3.md
)

target_include_directories(${PROJECT_NAME} PRIVATE "${PLANETARY_LIBS}/p_pds")
