# SPDX-License-Identifier: MulanPSL-2.0

# iRT keeps its routing algorithm and legacy iDB path in Operation. This
# target provides the EccDB implementation of the backend-neutral source.
add_library(eccdb_irt_adapter STATIC RTInterfaceEccdb.cpp)

target_compile_features(eccdb_irt_adapter PUBLIC cxx_std_20)

target_include_directories(
  eccdb_irt_adapter
  PUBLIC
    ${ECCDB_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(
  eccdb_irt_adapter
  PUBLIC
    irt_interface
  PRIVATE
    irt_data_manager
    irt_source
    idrc_interface
    eccdb_idrc_adapter
    ecc_feature
    idm
    eccdb_core
)

if(ECCDB_BUILD_TESTS)
  add_subdirectory(
    ${ECCDB_SOURCE_DIR}/tests/adapters/irt
    ${CMAKE_CURRENT_BINARY_DIR}/tests
  )
endif()

if(ECCDB_BUILD_BENCHMARKS)
  add_executable(irt_input_benchmark
    ${ECCDB_SOURCE_DIR}/benchmarks/IrtInputBenchmark.cpp
  )
  target_compile_features(irt_input_benchmark PUBLIC cxx_std_20)
  target_link_libraries(irt_input_benchmark
    PRIVATE
      eccdb_irt_adapter
      irt_data_manager
      irt_source
      idrc_interface
      idm
      ecc_feature
      eccdb_core
      eccdb_lef_import
      eccdb_def_import
  )
endif()
