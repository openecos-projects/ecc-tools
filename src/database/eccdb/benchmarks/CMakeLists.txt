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
  if(ECCDB_BUILD_TESTS)
    add_executable(eccdb_drt_trace_test DrtTraceTest.cpp)
    target_compile_features(eccdb_drt_trace_test PRIVATE cxx_std_20)
    target_link_libraries(eccdb_drt_trace_test PRIVATE ${ECCDB_BOOST_JSON_LIBRARY})
    add_test(NAME eccdb_drt_trace_test COMMAND eccdb_drt_trace_test)
  endif()
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
  set_property(TARGET eccdb_db_only_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
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

if(TARGET IdbBuilder AND ECCDB_BOOST_JSON_LIBRARY)
  # Parser-excluded logical-netlist counterpart to DbOnlyEccdbBenchmark.
  # IdbBuilder brings the legacy iDB data model and its dependencies into a
  # complete top-level build; standalone EccDB configurations omit this.
  add_executable(eccdb_db_only_idb_benchmark
    DbOnlyIdbBenchmark.cpp
  )
  eccdb_storage_target(eccdb_db_only_idb_benchmark)
  target_include_directories(eccdb_db_only_idb_benchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/data_manager
  )
  target_link_libraries(eccdb_db_only_idb_benchmark
    PRIVATE
      IdbBuilder
      ${ECCDB_BOOST_JSON_LIBRARY}
  )
  set_property(TARGET eccdb_db_only_idb_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

  if(TARGET eccdb_lef_import AND TARGET eccdb_def_import
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/IdbComparisonBenchmark.cpp")
    add_executable(eccdb_idb_comparison_benchmark
      IdbComparisonBenchmark.cpp
    )
    eccdb_storage_target(eccdb_idb_comparison_benchmark)
    target_include_directories(eccdb_idb_comparison_benchmark PRIVATE
      ${PROJECT_SOURCE_DIR}/src/platform/data_manager
    )
    target_link_libraries(eccdb_idb_comparison_benchmark
      PRIVATE
        eccdb_lef_import
        eccdb_def_import
        IdbBuilder
        ${ECCDB_BOOST_JSON_LIBRARY}
    )
    set_property(TARGET eccdb_idb_comparison_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
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
