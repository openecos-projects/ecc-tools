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

add_library(eccdb_lef_export
  LefExportFormat.cpp
  LefLibraryExporter.cpp
  LefTechExporter.cpp
)
eccdb_storage_target(eccdb_lef_export)
set_target_properties(eccdb_lef_export PROPERTIES EXPORT_NAME lef_export)
target_link_libraries(eccdb_lef_export
  PUBLIC
    eccdb_library
    eccdb_tech_database
)

if(TARGET lef)
  # Keep the SI2 parser private to this shared boundary.  An exported static
  # importer would otherwise expose the non-exported in-tree `lef` target to
  # every installed consumer.
  add_library(eccdb_lef_import SHARED
    LefLibraryImporter.cpp
    LefTechImporter.cpp
    detail/LefParserMutex.cpp
    detail/LefTechLayerProperties.cpp
    detail/LefTechLayerRules.cpp
    detail/LefTechObjects.cpp
  )
  eccdb_storage_target(eccdb_lef_import)
  set_target_properties(eccdb_lef_import PROPERTIES EXPORT_NAME lef_import)
  if(UNIX AND NOT APPLE)
    set_target_properties(eccdb_lef_import PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()

  target_link_libraries(eccdb_lef_import
    PUBLIC
      eccdb_library
      eccdb_tech_database
    PRIVATE
      lef
  )
endif()
