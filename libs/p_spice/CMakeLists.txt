cmake_minimum_required(VERSION 3.22)
project(p_spice C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

set(CSPICE_DIR "$ENV{HOME}/dev/cspice")

add_library(p_spice SHARED p_spice.c p_spice.h)

target_include_directories(p_spice PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
  "${CSPICE_DIR}/include"
)

target_link_libraries(p_spice PRIVATE
  GRASS::GIS
  LIBM
  "${CSPICE_DIR}/lib/cspice.a"
)

target_compile_features(p_spice PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_spice PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_spice PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_spice
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_spice.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass")

if(BUILD_TESTING)
  add_executable(test_p_spice test_p_spice.c)
  target_include_directories(test_p_spice PRIVATE "${CSPICE_DIR}/include")
  target_link_libraries(test_p_spice PRIVATE p_spice GRASS::GIS LIBM
                        "${CSPICE_DIR}/lib/cspice.a")
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_spice PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_spice COMMAND test_p_spice)
endif()
