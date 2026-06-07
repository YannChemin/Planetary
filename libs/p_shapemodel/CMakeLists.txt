cmake_minimum_required(VERSION 3.22)
project(p_shapemodel C)
include(build_addon OPTIONAL)

find_package(OpenMP)
find_M()

add_library(p_shapemodel SHARED p_shapemodel.c p_shapemodel.h)

target_include_directories(p_shapemodel PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(p_shapemodel PRIVATE GRASS::GIS LIBM)
target_compile_features(p_shapemodel PRIVATE c_std_99)

if(OpenMP_C_FOUND)
  target_compile_options(p_shapemodel PRIVATE ${OpenMP_C_FLAGS})
  target_link_libraries(p_shapemodel PRIVATE OpenMP::OpenMP_C)
endif()

include(GNUInstallDirs)
install(TARGETS p_shapemodel
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES p_shapemodel.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/grass")

if(BUILD_TESTING)
  add_executable(test_p_shapemodel test_p_shapemodel.c)
  target_link_libraries(test_p_shapemodel PRIVATE p_shapemodel GRASS::GIS LIBM)
  if(OpenMP_C_FOUND)
    target_link_libraries(test_p_shapemodel PRIVATE OpenMP::OpenMP_C)
  endif()
  add_test(NAME test_p_shapemodel COMMAND test_p_shapemodel)
endif()
