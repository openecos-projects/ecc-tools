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

add_library(eccdb_binary_persistence
  BinaryFormat.cpp
  BinaryPayload.cpp
)
eccdb_storage_target(eccdb_binary_persistence)
set_target_properties(eccdb_binary_persistence PROPERTIES EXPORT_NAME binary_persistence)
target_link_libraries(eccdb_binary_persistence
  PUBLIC
    eccdb_boost_pfr
    eccdb_design
    eccdb_entt
    eccdb_library
    eccdb_tech_database
)

add_library(eccdb_binary_import
  BinaryDatabaseImporter.cpp
)
eccdb_storage_target(eccdb_binary_import)
set_target_properties(eccdb_binary_import PROPERTIES EXPORT_NAME binary_import)
target_link_libraries(eccdb_binary_import PUBLIC eccdb_binary_persistence)

add_library(eccdb_binary_export
  BinaryDatabaseExporter.cpp
)
eccdb_storage_target(eccdb_binary_export)
set_target_properties(eccdb_binary_export PROPERTIES EXPORT_NAME binary_export)
target_link_libraries(eccdb_binary_export PUBLIC eccdb_binary_persistence)
