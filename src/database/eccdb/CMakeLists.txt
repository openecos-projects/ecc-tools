# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Configure from a separate build directory.")
endif()

set(ECCDB_STANDALONE OFF)
set(ECCDB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
# The native Database owner is shared even when the surrounding project uses
# static targets. Its private dependency closure must therefore be
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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
      $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}>
      $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}/storage>
      $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}/service>
      $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}/io>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/storage>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/service>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/io>
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
    $<BUILD_INTERFACE:${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/third_party/entt>
)
set_target_properties(eccdb_entt PROPERTIES EXPORT_NAME entt)

add_library(eccdb_boost_pfr INTERFACE)
target_compile_features(eccdb_boost_pfr INTERFACE cxx_std_20)
set_target_properties(eccdb_boost_pfr PROPERTIES EXPORT_NAME boost_pfr)
# Hosts can define Boost::headers as an alias for a vendored subset (slang in
# OpenROAD does this). Resolve the actual PFR headers without exporting that
# host target or a build-machine include path into the installed SDK.
find_path(ECCDB_BOOST_INCLUDE_DIR NAMES boost/pfr.hpp
  HINTS ${Boost_INCLUDE_DIRS} REQUIRED)
mark_as_advanced(ECCDB_BOOST_INCLUDE_DIR)
target_include_directories(eccdb_boost_pfr SYSTEM INTERFACE
  $<BUILD_INTERFACE:${ECCDB_BOOST_INCLUDE_DIR}>)

set(_eccdb_default_build_tests ON)
set(_eccdb_default_install_sdk ON)
if("${SKBUILD}" STREQUAL "2")
  # A wheel ships the runtime extension, not the native SDK or its test corpus.
  set(_eccdb_default_build_tests OFF)
  set(_eccdb_default_install_sdk OFF)
endif()
option(ECCDB_BUILD_TESTS "Build ECCDB tests" ${_eccdb_default_build_tests})
option(ECCDB_BUILD_BENCHMARKS "Build ECCDB performance benchmarks" OFF)
option(ECCDB_INSTALL_SDK "Install the native EccDB SDK and CMake package" ${_eccdb_default_install_sdk})
unset(_eccdb_default_build_tests)
unset(_eccdb_default_install_sdk)

add_subdirectory(storage)
add_subdirectory(service)

# The native SDK boundary is the stores plus their single-threaded services.
# Keep this interface target separate from the Database owner so native users
# can construct stores and services without acquiring a second DTO layer.
add_library(eccdb_core INTERFACE)
add_library(EccDB::core ALIAS eccdb_core)
set_target_properties(eccdb_core PROPERTIES EXPORT_NAME core)
target_compile_features(eccdb_core INTERFACE cxx_std_20)
target_link_libraries(eccdb_core
  INTERFACE
    eccdb_context
    eccdb_service
    eccdb_geometry
    eccdb_design
    eccdb_library
    eccdb_tech_database
)

# Operation-facing EccDB adaptors live at the SDK boundary.  They are only
# meaningful in the full ecc-tools build, where the corresponding Operation
# targets are provided later by src/operation; the adaptor CMake file keeps
# those target names as explicit link dependencies.
add_subdirectory(adapters)

add_subdirectory(io/binary)
add_subdirectory(io/idb)
add_subdirectory(io/lef)
add_subdirectory(io/def)
add_subdirectory(database)
add_subdirectory(viewer)

if(ECCDB_BUILD_TESTS)
  add_subdirectory(tests/unit/geometry)
  add_subdirectory(tests/unified_geometry)
  add_subdirectory(tests/unit/design)
  if(TARGET eccdb_binary_export AND TARGET eccdb_binary_import
     AND TARGET eccdb_lef_export AND TARGET eccdb_lef_import)
    add_subdirectory(tests/io/binary)
  endif()
  if(TARGET eccdb_def_import AND TARGET eccdb_def_export)
    add_subdirectory(tests/io/def)
  endif()
  add_subdirectory(tests/differential)
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
  eccdb_service
  eccdb_database_owner
  eccdb_viewer
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

set(ECCDB_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/EccDB"
    CACHE STRING "Install directory for the EccDB CMake package")

set(_eccdb_export_targets
  eccdb_entt
  eccdb_boost_pfr
  eccdb_geometry
  eccdb_tech_layers
  eccdb_tech_global
  eccdb_tech_via_master
  eccdb_tech_via_rule
  eccdb_tech_via_rule_generate
  eccdb_tech_non_default_rule
  eccdb_tech_database
  eccdb_library
  eccdb_design
  eccdb_context
  eccdb_service
  eccdb_core
  eccdb_binary_persistence
  eccdb_binary_import
  eccdb_binary_export
  eccdb_lef_export
  eccdb_def_export
  eccdb_database_owner
  eccdb_viewer
)
foreach(_target IN ITEMS eccdb_lef_import eccdb_def_import)
  if(TARGET ${_target})
    list(APPEND _eccdb_export_targets ${_target})
  endif()
endforeach()

# Licenses accompany both the native SDK and the Python runtime wheel.
install(FILES
  "${ECCDB_SOURCE_DIR}/LICENSE"
  "${ECCDB_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/eccdb"
)
install(FILES "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt/LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/eccdb/entt"
)
install(FILES
  "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef/LICENSE"
  "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef/lef/LICENSE.TXT"
  "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef/def/LICENSE.TXT"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/eccdb/lefdef"
)

if(NOT ECCDB_INSTALL_SDK)
  # ecc_py loads the database owner and optional LEF/DEF import boundaries.
  # Keep SDK-only shared targets, such as eccdb_viewer, out of the wheel.
  set(_eccdb_runtime_targets eccdb_database_owner)
  foreach(_target IN ITEMS eccdb_lef_import eccdb_def_import)
    if(TARGET ${_target})
      list(APPEND _eccdb_runtime_targets ${_target})
    endif()
  endforeach()
  if(_eccdb_runtime_targets)
    install(TARGETS ${_eccdb_runtime_targets}
      LIBRARY DESTINATION "ecc_tools_bin/lib"
      RUNTIME DESTINATION "ecc_tools_bin/lib"
    )
  endif()
  return()
endif()

install(TARGETS ${_eccdb_export_targets}
  EXPORT EccDBTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY "${ECCDB_SOURCE_DIR}/database/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/database"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
install(FILES "${ECCDB_SOURCE_DIR}/eccdb.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb"
)
install(DIRECTORY "${ECCDB_SOURCE_DIR}/storage/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/storage"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
install(DIRECTORY "${ECCDB_SOURCE_DIR}/service/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/service"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
foreach(_eccdb_io_header_dir IN ITEMS binary lef def)
  install(DIRECTORY "${ECCDB_SOURCE_DIR}/io/${_eccdb_io_header_dir}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/io/${_eccdb_io_header_dir}"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
  )
endforeach()
unset(_eccdb_io_header_dir)
install(DIRECTORY "${ECCDB_SOURCE_DIR}/index/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/index"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
install(FILES "${ECCDB_SOURCE_DIR}/viewer/DesignModelAdapter.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/viewer"
)
install(DIRECTORY "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/entt/entt/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eccdb/third_party/entt/entt"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
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
