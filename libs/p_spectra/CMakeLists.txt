cmake_minimum_required(VERSION 3.22)
project(p_spectra C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

add_library(p_spectra SHARED p_spectra.c p_spectra.h)
target_include_directories(p_spectra PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(p_spectra PRIVATE GRASS::GIS LIBM)
target_compile_features(p_spectra PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_spectra PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_spectra PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_spectra
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_spectra.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass")

if(BUILD_TESTING)
  add_executable(test_p_spectra test_p_spectra.c)
  target_link_libraries(test_p_spectra PRIVATE p_spectra GRASS::GIS LIBM)
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_spectra PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_spectra COMMAND test_p_spectra)
endif()
