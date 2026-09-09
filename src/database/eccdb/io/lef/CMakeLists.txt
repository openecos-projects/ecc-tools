add_library(eccdb_lef_export
  LefExportFormat.cpp
  LefLibraryExporter.cpp
  LefTechExporter.cpp
)
eccdb_storage_target(eccdb_lef_export)
target_link_libraries(eccdb_lef_export
  PUBLIC
    eccdb_library
    eccdb_tech_database
)

if(TARGET lef)
  add_library(eccdb_lef_import
    LefLibraryImporter.cpp
    LefTechImporter.cpp
    detail/LefParserMutex.cpp
    detail/LefTechLayerProperties.cpp
    detail/LefTechLayerRules.cpp
    detail/LefTechObjects.cpp
  )
  eccdb_storage_target(eccdb_lef_import)

  target_link_libraries(eccdb_lef_import
    PUBLIC
      eccdb_library
      eccdb_tech_database
    PRIVATE
      lef
  )
endif()
