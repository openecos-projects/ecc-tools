# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

add_library(eccdb_viewer SHARED
  "${ECCDB_SOURCE_DIR}/viewer/DesignModelAdapter.cpp"
  "${ECCDB_SOURCE_DIR}/viewer/ViewerLibrary.cpp"
)
add_library(EccDB::viewer ALIAS eccdb_viewer)
set_target_properties(eccdb_viewer PROPERTIES
  EXPORT_NAME viewer
  OUTPUT_NAME eccdb_viewer
  VERSION "0.1.0"
  SOVERSION "0"
  POSITION_INDEPENDENT_CODE ON
)
target_compile_features(eccdb_viewer PUBLIC cxx_std_20)
target_include_directories(eccdb_viewer
  PUBLIC
    $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb>
  PRIVATE
    "${ECCDB_SOURCE_DIR}/storage"
    "${ECCDB_SOURCE_DIR}/io"
    "${ECCDB_SOURCE_DIR}/service"
    "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt"
)
target_link_libraries(eccdb_viewer
  PRIVATE
    EccDB::database
  PUBLIC
    eccdb_core
)
if(UNIX AND NOT APPLE)
  set_target_properties(eccdb_viewer PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

include(GNUInstallDirs)
option(ECCDB_BUILD_VIEWER_PYTHON "Build the optional standalone eccdb_viewer Python module" OFF)
if(ECCDB_BUILD_VIEWER_PYTHON)
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
  if(NOT TARGET pybind11::module)
    add_subdirectory("${ECCDB_ECC_TOOLS_ROOT}/src/third_party/pybind11"
                     "${CMAKE_BINARY_DIR}/eccdb_viewer_pybind11")
  endif()
  pybind11_add_module(eccdb_viewer_py MODULE PythonModule.cpp)
  target_compile_features(eccdb_viewer_py PRIVATE cxx_std_20)
  target_include_directories(eccdb_viewer_py PRIVATE "${ECCDB_SOURCE_DIR}/service")
  target_link_libraries(eccdb_viewer_py PRIVATE eccdb_viewer EccDB::database pybind11::module)
  set_target_properties(eccdb_viewer_py PROPERTIES OUTPUT_NAME eccdb_viewer)
  if(UNIX AND NOT APPLE)
    set_target_properties(eccdb_viewer_py PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
  if(ECCDB_BUILD_TESTS AND TARGET eccdb_lef_import AND TARGET eccdb_def_import)
    add_test(NAME eccdb_service_bindings_smoke
      COMMAND "${CMAKE_COMMAND}" -E env
              "PYTHONPATH=$<TARGET_FILE_DIR:eccdb_viewer_py>"
              "${Python3_EXECUTABLE}"
              "${ECCDB_SOURCE_DIR}/tests/unified_geometry/ServiceBindingsSmoke.py")
  endif()
  install(TARGETS eccdb_viewer_py
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
