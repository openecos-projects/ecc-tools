if(TARGET eccdb_lef_import AND TARGET eccdb_def_import AND TARGET eccdb_lef_export AND TARGET eccdb_def_export)
  add_executable(eccdb_benchmark
    EccdbBenchmark.cpp
  )

  eccdb_storage_target(eccdb_benchmark)
  target_include_directories(eccdb_benchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/data_manager
  )
  target_link_libraries(eccdb_benchmark
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
      eccdb_lef_export
      eccdb_def_export
  )

  if(TARGET IdbBuilder)
    target_compile_definitions(eccdb_benchmark PRIVATE ECCDB_BENCHMARK_HAS_LEGACY_IDB=1)
    target_link_libraries(eccdb_benchmark PRIVATE IdbBuilder)
  endif()
endif()

if(TARGET eccdb_lef_import AND TARGET eccdb_def_import
   AND TARGET eccdb_binary_export AND TARGET eccdb_binary_import
   AND TARGET eccdb_def_export)
  add_executable(eccdb_binary_archive_benchmark
    BinaryArchiveBenchmark.cpp
  )

  eccdb_storage_target(eccdb_binary_archive_benchmark)
  target_link_libraries(eccdb_binary_archive_benchmark
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
      eccdb_binary_export
      eccdb_binary_import
      eccdb_def_export
  )
endif()
