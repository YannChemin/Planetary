cmake_minimum_required(VERSION 3.22)
project(p_projection_planet C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

add_library(p_projection_planet SHARED
  p_projection_planet.c
  p_projection_planet.h
)
target_include_directories(p_projection_planet PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(p_projection_planet PRIVATE GRASS::GIS LIBM)
target_compile_features(p_projection_planet PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_projection_planet PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_projection_planet PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_projection_planet
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_projection_planet.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass")

if(BUILD_TESTING)
  add_executable(test_p_projection_planet test_p_projection_planet.c)
  target_link_libraries(test_p_projection_planet PRIVATE p_projection_planet GRASS::GIS LIBM)
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_projection_planet PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_projection_planet COMMAND test_p_projection_planet)
endif()
