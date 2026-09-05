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
