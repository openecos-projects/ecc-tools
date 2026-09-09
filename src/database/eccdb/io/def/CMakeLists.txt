add_library(eccdb_def_export
  DefDesignExporter.cpp
)
eccdb_storage_target(eccdb_def_export)
target_link_libraries(eccdb_def_export
  PUBLIC
    eccdb_design
)

if(TARGET def)
  add_library(eccdb_def_import
    DefDesignImporter.cpp
    detail/DefParserMutex.cpp
  )
  eccdb_storage_target(eccdb_def_import)
  target_link_libraries(eccdb_def_import
    PUBLIC
      eccdb_design
    PRIVATE
      def
  )
endif()
