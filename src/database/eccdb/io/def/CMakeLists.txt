# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

add_library(eccdb_def_export
  DefDesignExporter.cpp
)
eccdb_storage_target(eccdb_def_export)
set_target_properties(eccdb_def_export PROPERTIES EXPORT_NAME def_export)
target_link_libraries(eccdb_def_export
  PUBLIC
    eccdb_design
    eccdb_service
)

if(TARGET def)
  # Keep the SI2 parser private to this shared boundary.  An exported static
  # importer would otherwise expose the non-exported in-tree `def` target to
  # every installed consumer.
  add_library(eccdb_def_import SHARED
    DefDesignImporter.cpp
    detail/DefParserMutex.cpp
  )
  eccdb_storage_target(eccdb_def_import)
  set_target_properties(eccdb_def_import PROPERTIES EXPORT_NAME def_import)
  if(UNIX AND NOT APPLE)
    set_target_properties(eccdb_def_import PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
  target_link_libraries(eccdb_def_import
    PUBLIC
      eccdb_design
      eccdb_service
    PRIVATE
      def
  )
endif()
