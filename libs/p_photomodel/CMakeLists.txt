cmake_minimum_required(VERSION 3.22)
project(p_photomodel C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

add_library(p_photomodel SHARED
  p_photomodel.c
  p_photomodel.h
)

target_include_directories(p_photomodel PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(p_photomodel PRIVATE
  GRASS::GIS
  LIBM
)

target_compile_features(p_photomodel PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_photomodel PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_photomodel PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_photomodel
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_photomodel.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass"
)

if(BUILD_TESTING)
  add_executable(test_p_photomodel test_p_photomodel.c)
  target_link_libraries(test_p_photomodel PRIVATE p_photomodel GRASS::GIS LIBM)
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_photomodel PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_photomodel COMMAND test_p_photomodel)
endif()
