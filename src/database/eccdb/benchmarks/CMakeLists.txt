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

if(TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  # This intentionally measures only the LEF/DEF import path.  The paired
  # OpenDB measurement lives in IspdImportComparison.py so both backends can
  # consume the same repaired ISPD18 LEF.
  add_executable(eccdb_ispd18_import_benchmark
    IspdImportBenchmark.cpp
  )
  eccdb_storage_target(eccdb_ispd18_import_benchmark)
  target_link_libraries(eccdb_ispd18_import_benchmark
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
  )

  add_executable(eccdb_path_record_benchmark
    PathRecordLayoutBenchmark.cpp
  )
  eccdb_storage_target(eccdb_path_record_benchmark)
  target_link_libraries(eccdb_path_record_benchmark
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
  )

  find_path(ECCDB_BOOST_SMALL_VECTOR_INCLUDE_DIR boost/container/small_vector.hpp)
  if(ECCDB_BOOST_SMALL_VECTOR_INCLUDE_DIR)
    add_executable(eccdb_path_record_inline_benchmark
      PathRecordLayoutBenchmark.cpp
    )
    eccdb_storage_target(eccdb_path_record_inline_benchmark)
    target_compile_definitions(eccdb_path_record_inline_benchmark PRIVATE ECCDB_PATH_RECORD_INLINE_TWO=1)
    target_include_directories(eccdb_path_record_inline_benchmark PRIVATE ${ECCDB_BOOST_SMALL_VECTOR_INCLUDE_DIR})
    target_link_libraries(eccdb_path_record_inline_benchmark
      PRIVATE
        eccdb_lef_import
        eccdb_def_import
    )
  endif()
endif()

find_library(ECCDB_BOOST_JSON_LIBRARY NAMES boost_json)
if(ECCDB_BOOST_JSON_LIBRARY)
  add_executable(eccdb_db_only_benchmark
    DbOnlyEccdbBenchmark.cpp
  )
  eccdb_storage_target(eccdb_db_only_benchmark)
  target_link_libraries(eccdb_db_only_benchmark
    PRIVATE
      eccdb_design
      eccdb_tech_database
      ${ECCDB_BOOST_JSON_LIBRARY}
  )
endif()

if(TARGET eccdb_lef_import AND TARGET eccdb_def_import AND ECCDB_BOOST_JSON_LIBRARY)
  add_executable(eccdb_drt_service_benchmark
    DrtServiceEccdbBenchmark.cpp
  )
  eccdb_storage_target(eccdb_drt_service_benchmark)
  target_link_libraries(eccdb_drt_service_benchmark
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
      eccdb_service
      ${ECCDB_BOOST_JSON_LIBRARY}
  )
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
