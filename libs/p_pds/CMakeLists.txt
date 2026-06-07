cmake_minimum_required(VERSION 3.22)
project(p_pds C)
include(build_addon OPTIONAL)

find_M()

add_library(p_pds SHARED
  p_pds.c
  p_pds.h
)

target_include_directories(p_pds PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(p_pds PRIVATE
  GRASS::GIS
  LIBM
)

target_compile_features(p_pds PRIVATE c_std_99)

# Install targets
include(GNUInstallDirs)
install(TARGETS p_pds
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_pds.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass"
)

# Optional: build a self-contained unit test binary
if(BUILD_TESTING)
  add_executable(test_p_pds test_p_pds.c)
  target_link_libraries(test_p_pds PRIVATE p_pds GRASS::GIS LIBM)
  add_test(NAME test_p_pds COMMAND test_p_pds)
endif()
