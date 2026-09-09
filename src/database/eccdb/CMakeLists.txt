cmake_minimum_required(VERSION 3.20)

set(ECCDB_STANDALONE OFF)
set(ECCDB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
# The public API is a shared library even when the surrounding project uses
# static internal targets. Its private dependency closure must therefore be
# position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(ECCDB_STANDALONE ON)
  project(eccdb LANGUAGES CXX)
  enable_testing()
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Boost is a project-provided header dependency. PFR is consumed from the
# system Boost installation together with the other Boost headers used by the
# eccdb (Polygon, Spirit, and Variant).
find_package(Boost 1.75 REQUIRED)

get_filename_component(_eccdb_default_ecc_tools_root
  "${CMAKE_CURRENT_LIST_DIR}/../../.."
  ABSOLUTE
)
set(ECCDB_ECC_TOOLS_ROOT "${_eccdb_default_ecc_tools_root}" CACHE PATH
    "Path to the ecc-tools source tree")

function(eccdb_storage_target target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_include_directories(${target_name}
    PUBLIC
      ${ECCDB_SOURCE_DIR}
      ${ECCDB_SOURCE_DIR}/include
      ${ECCDB_SOURCE_DIR}/storage
      ${ECCDB_SOURCE_DIR}/io
      "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt"
  )
endfunction()

set(ECCDB_TEST_DATA_ROOT "${ECCDB_ECC_TOOLS_ROOT}" CACHE PATH
    "Path containing the scripts/foundry and scripts/design test corpora")

option(ECCDB_STANDALONE_LEF_DEF
  "In a standalone configure, build the SI2 parsers and LEF/DEF differential tests"
  OFF
)
option(ECCDB_STANDALONE_LEGACY_IDB
  "In a standalone configure, build the minimal legacy iDB LEF adapter and differential tests"
  OFF
)

if(ECCDB_STANDALONE_LEGACY_IDB AND NOT ECCDB_STANDALONE_LEF_DEF)
  set(ECCDB_STANDALONE_LEF_DEF ON CACHE BOOL
      "In a standalone configure, build the SI2 parsers and LEF/DEF differential tests" FORCE)
endif()

if(ECCDB_STANDALONE AND ECCDB_STANDALONE_LEF_DEF)
  include(cmake/StandaloneDependencies.cmake)
  eccdb_add_standalone_lef_def_dependencies()
  if(ECCDB_STANDALONE_LEGACY_IDB)
    eccdb_add_standalone_legacy_idb_dependencies()
  endif()
endif()

add_library(eccdb_entt INTERFACE)
target_compile_features(eccdb_entt INTERFACE cxx_std_20)
target_include_directories(eccdb_entt
  SYSTEM
  INTERFACE
    "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt"
)

add_library(eccdb_boost_pfr INTERFACE)
target_compile_features(eccdb_boost_pfr INTERFACE cxx_std_20)
if(TARGET Boost::headers)
  target_link_libraries(eccdb_boost_pfr INTERFACE Boost::headers)
elseif(TARGET Boost::boost)
  target_link_libraries(eccdb_boost_pfr INTERFACE Boost::boost)
else()
  target_include_directories(eccdb_boost_pfr SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
endif()

option(ECCDB_BUILD_TESTS "Build ECCDB tests" ON)
option(ECCDB_BUILD_BENCHMARKS "Build ECCDB performance benchmarks" OFF)

add_subdirectory(storage)
add_subdirectory(io/binary)
add_subdirectory(io/idb)
add_subdirectory(io/lef)
add_subdirectory(io/def)
add_subdirectory(api)

if(ECCDB_BUILD_TESTS)
  add_subdirectory(tests/unit/geometry)
  add_subdirectory(tests/unit/design)
  add_subdirectory(tests/unit/library)
  add_subdirectory(tests/unit/tech)
  if(TARGET eccdb_def_import AND TARGET eccdb_def_export)
    add_subdirectory(tests/io/def)
  endif()
  if(TARGET eccdb_lef_import)
    add_subdirectory(tests/io/lef)
  endif()
  if(TARGET eccdb_binary_export AND TARGET eccdb_binary_import AND TARGET eccdb_lef_import)
    add_subdirectory(tests/io/binary)
  endif()
  add_subdirectory(tests/differential)
  add_subdirectory(tests/memory)
endif()

if(ECCDB_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

set(_eccdb_database_targets
  eccdb_binary_export
  eccdb_binary_import
  eccdb_binary_persistence
  eccdb_def_export
  eccdb_design
  eccdb_lef_export
  eccdb_library
  eccdb_api
  eccdb_tech_database
)
foreach(_target IN ITEMS
    eccdb_def_import
    eccdb_idb_import
    eccdb_legacy_lef_reader
    eccdb_lef_import)
  if(TARGET ${_target})
    list(APPEND _eccdb_database_targets ${_target})
  endif()
endforeach()
add_custom_target(eccdb_database
  DEPENDS ${_eccdb_database_targets}
)

add_custom_target(eccdb_via_master DEPENDS eccdb_tech_via_master)
add_custom_target(eccdb_via_rule DEPENDS eccdb_tech_via_rule)
add_custom_target(eccdb_via_rule_generate DEPENDS eccdb_tech_via_rule_generate)
add_custom_target(eccdb_tech_layer DEPENDS eccdb_tech_layers)
add_custom_target(eccdb_tech_database_api DEPENDS eccdb_tech_database)
add_custom_target(eccdb_non_default_rule DEPENDS eccdb_tech_non_default_rule)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(ECCDB_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/EccDB"
    CACHE STRING "Install directory for the EccDB CMake API package")

install(TARGETS eccdb_api
  EXPORT EccDBTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(DIRECTORY "${ECCDB_SOURCE_DIR}/include/eccdb"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h"
)

configure_package_config_file(
  "${ECCDB_SOURCE_DIR}/cmake/EccDBConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/EccDBConfig.cmake"
  INSTALL_DESTINATION "${ECCDB_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/EccDBConfigVersion.cmake"
  VERSION "0.1.0"
  COMPATIBILITY SameMajorVersion
)
install(EXPORT EccDBTargets
  FILE EccDBTargets.cmake
  NAMESPACE EccDB::
  DESTINATION "${ECCDB_INSTALL_CMAKEDIR}"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/EccDBConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/EccDBConfigVersion.cmake"
  DESTINATION "${ECCDB_INSTALL_CMAKEDIR}"
)
