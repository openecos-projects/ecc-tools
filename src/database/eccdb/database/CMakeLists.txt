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

add_library(eccdb_database_owner SHARED
  Database.cpp
)
add_library(EccDB::database ALIAS eccdb_database_owner)
set_target_properties(eccdb_database_owner PROPERTIES
  EXPORT_NAME database
  OUTPUT_NAME eccdb
  VERSION "0.1.0"
  SOVERSION "0"
  POSITION_INDEPENDENT_CODE ON
)
if(UNIX AND NOT APPLE)
  set_target_properties(eccdb_database_owner PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
target_compile_features(eccdb_database_owner PUBLIC cxx_std_20)
target_include_directories(eccdb_database_owner
  PUBLIC
    $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/eccdb>
)
target_link_libraries(eccdb_database_owner
  PUBLIC
    eccdb_core
  PRIVATE
    eccdb_binary_export
    eccdb_binary_import
    eccdb_def_export
)

if(TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  target_compile_definitions(eccdb_database_owner PRIVATE ECCDB_HAS_LEF_DEF=1)
  target_link_libraries(eccdb_database_owner PRIVATE eccdb_lef_import eccdb_def_import)
else()
  target_compile_definitions(eccdb_database_owner PRIVATE ECCDB_HAS_LEF_DEF=0)
endif()

if(TARGET verilog)
  target_compile_definitions(eccdb_database_owner PRIVATE ECCDB_HAS_VERILOG=1)
  target_link_libraries(eccdb_database_owner PRIVATE eccdb_verilog_import)
else()
  target_compile_definitions(eccdb_database_owner PRIVATE ECCDB_HAS_VERILOG=0)
endif()
