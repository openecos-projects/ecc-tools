add_library(eccdb_binary_persistence
  BinaryFormat.cpp
  BinaryPayload.cpp
)
eccdb_storage_target(eccdb_binary_persistence)
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
target_link_libraries(eccdb_binary_import PUBLIC eccdb_binary_persistence)

add_library(eccdb_binary_export
  BinaryDatabaseExporter.cpp
)
eccdb_storage_target(eccdb_binary_export)
target_link_libraries(eccdb_binary_export PUBLIC eccdb_binary_persistence)
