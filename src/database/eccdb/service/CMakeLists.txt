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

add_library(eccdb_context context/DatabaseContext.cpp)
eccdb_storage_target(eccdb_context)
set_target_properties(eccdb_context PROPERTIES EXPORT_NAME context)
target_include_directories(eccdb_context
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/service>
)
target_link_libraries(eccdb_context PUBLIC eccdb_design eccdb_tech_database eccdb_library)

add_library(eccdb_service
  instance/InstanceService.cpp
  constraint/ConstraintService.cpp
  floorplan/FloorplanService.cpp
  connectivity/ConnectivityService.cpp
  geometry/GeometryQueryService.cpp
  geometry/GeometryPayloadService.cpp
  geometry/GeometryOccurrenceService.cpp
  geometry/GeometryHitTestService.cpp
  geometry/GeometryEditService.cpp
  geometry/GeometryMutation.cpp
  geometry/PhysicalDesignService.cpp
  routing/RoutingService.cpp
)
eccdb_storage_target(eccdb_service)
set_target_properties(eccdb_service PROPERTIES EXPORT_NAME service)
target_include_directories(eccdb_service
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb/service>
)
target_link_libraries(eccdb_service PUBLIC eccdb_context eccdb_design eccdb_tech_database eccdb_library)

# The structural Verilog importer is kept in a small optional target.  Making
# it separate avoids exporting the legacy Flex/Bison parser as a transitive
# dependency of the standalone EccDB service SDK.
if(TARGET verilog)
  add_library(eccdb_verilog_import STATIC
    import/verilog/VerilogNetlistImportService.cpp
  )
  eccdb_storage_target(eccdb_verilog_import)
  set_target_properties(eccdb_verilog_import PROPERTIES EXPORT_NAME verilog_import)
  target_link_libraries(eccdb_verilog_import PRIVATE eccdb_service verilog)
endif()
