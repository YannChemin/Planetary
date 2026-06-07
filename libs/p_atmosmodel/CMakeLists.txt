cmake_minimum_required(VERSION 3.22)
project(p_atmosmodel C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

add_library(p_atmosmodel SHARED
  p_atmosmodel.c
  p_atmosmodel.h
)

target_include_directories(p_atmosmodel PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(p_atmosmodel PRIVATE GRASS::GIS LIBM)
target_compile_features(p_atmosmodel PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_atmosmodel PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_atmosmodel PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_atmosmodel
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_atmosmodel.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass")

if(BUILD_TESTING)
  add_executable(test_p_atmosmodel test_p_atmosmodel.c)
  target_link_libraries(test_p_atmosmodel PRIVATE p_atmosmodel GRASS::GIS LIBM)
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_atmosmodel PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_atmosmodel COMMAND test_p_atmosmodel)
endif()
