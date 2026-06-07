cmake_minimum_required(VERSION 3.22)
project(p.in.pds4)
include(build_addon OPTIONAL)
find_M()
find_package(LibXml2 REQUIRED)

build_addon(
  ${PROJECT_NAME}
  SOURCES main.c
  GRASSLIBS gis raster
  DEPENDS LIBM
  DOCFILES p.in.pds4.html p.in.pds4.md
)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBXML2_LIBRARIES})
