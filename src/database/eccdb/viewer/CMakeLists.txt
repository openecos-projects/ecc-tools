add_library(eccdb_viewer SHARED
  "${ECCDB_SOURCE_DIR}/viewer/ViewerLibrary.cpp"
)
add_library(EccDB::viewer ALIAS eccdb_viewer)
set_target_properties(eccdb_viewer PROPERTIES
  OUTPUT_NAME eccdb_viewer
  VERSION "0.1.0"
  SOVERSION "0"
  POSITION_INDEPENDENT_CODE ON
)
target_compile_features(eccdb_viewer PUBLIC cxx_std_20)
target_include_directories(eccdb_viewer
  PUBLIC
    $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    "${ECCDB_SOURCE_DIR}"
    "${ECCDB_SOURCE_DIR}/storage"
    "${ECCDB_SOURCE_DIR}/io"
    "${ECCDB_SOURCE_DIR}/service"
    "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt"
)
target_link_libraries(eccdb_viewer PRIVATE EccDB::eccdb eccdb_service)

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
  target_link_libraries(eccdb_viewer_py PRIVATE eccdb_viewer eccdb_service EccDB::eccdb pybind11::module)
  set_target_properties(eccdb_viewer_py PROPERTIES OUTPUT_NAME eccdb_viewer)
  install(TARGETS eccdb_viewer_py
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS eccdb_viewer
  EXPORT EccDBTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
