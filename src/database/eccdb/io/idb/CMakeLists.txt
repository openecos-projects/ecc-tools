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

# The legacy adapter is unavailable until an iDB layout target is provided.
if(NOT TARGET idb)
  return()
endif()

add_library(eccdb_idb_import
  IdbTechImporter.cpp
  IdbLibraryImporter.cpp
)
eccdb_storage_target(eccdb_idb_import)
target_include_directories(eccdb_idb_import
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)
target_link_libraries(eccdb_idb_import
  PUBLIC
    eccdb_tech_database
    eccdb_library
  PRIVATE
    idb
)

if(TARGET lef_builder AND TARGET lef_service)
  add_library(eccdb_legacy_lef_reader
    LegacyLefReader.cpp
  )
  eccdb_storage_target(eccdb_legacy_lef_reader)
  target_include_directories(eccdb_legacy_lef_reader
    PUBLIC
      "${CMAKE_CURRENT_LIST_DIR}/../.."
  )
  target_link_libraries(eccdb_legacy_lef_reader
    PUBLIC
      lef_service
    PRIVATE
      idb
      lef_builder
  )
endif()
