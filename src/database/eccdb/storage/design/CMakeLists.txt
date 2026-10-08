# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(eccdb_design LANGUAGES CXX)
  enable_testing()

  if(NOT DEFINED ECCDB_BUILD_TESTS)
    option(ECCDB_BUILD_TESTS "Build ECCDB tests" ON)
  endif()

  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../library" "${CMAKE_CURRENT_BINARY_DIR}/library")
endif()

if(NOT TARGET eccdb_entt OR NOT TARGET eccdb_library)
  message(FATAL_ERROR "eccdb_design requires eccdb_entt and eccdb_library")
endif()

set(ECCDB_DESIGN_ENTITY_BITS "64" CACHE STRING
  "Design Entity storage width: 32 for compact mode or 64 for extended mode")
set_property(CACHE ECCDB_DESIGN_ENTITY_BITS PROPERTY STRINGS 32 64)

if(NOT ECCDB_DESIGN_ENTITY_BITS MATCHES "^(32|64)$")
  message(FATAL_ERROR "ECCDB_DESIGN_ENTITY_BITS must be 32 or 64")
endif()

add_library(eccdb_design
  DesignStore.cpp
  common/DesignGeometryValidation.cpp
  geometry/DesignGeometryStorage.cpp
  geometry/DesignGeometrySpatialIndex.cpp
  geometry/DesignGeometryOverview.cpp
  constraint/storage/DesignConstraintStorage.cpp
  fill/storage/DesignFillStorage.cpp
  floorplan/storage/DesignFloorplanStorage.cpp
  global/storage/DesignGlobalStorage.cpp
  guide/storage/DesignGuideStorage.cpp
  netlist/storage/DesignNetlistStorage.cpp
  pin_access/storage/DesignPinAccessStorage.cpp
  routing/component/WireRouting.cpp
  routing/storage/DesignRoutingStorage.cpp
  routing/view/WireRoutingView.cpp
)
target_compile_features(eccdb_design PUBLIC cxx_std_20)
set_target_properties(eccdb_design PROPERTIES EXPORT_NAME design)
target_include_directories(eccdb_design
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/storage>
)
target_compile_definitions(eccdb_design
  PUBLIC
    ECCDB_DESIGN_ENTITY_BITS=${ECCDB_DESIGN_ENTITY_BITS}
)
target_link_libraries(eccdb_design
  PUBLIC
    eccdb_entt
    eccdb_library
    eccdb_boost_pfr
)
