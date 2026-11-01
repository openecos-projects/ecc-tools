# SPDX-License-Identifier: MulanPSL-2.0

icts_add_test_executable(
  icts_eccdb_adapter_test
  SOURCES
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/icts/CtsEccdbAdapterTest.cpp
  LIBS
    eccdb_icts_adapter
    eccdb_core
)

target_include_directories(
  icts_eccdb_adapter_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/adapters/icts
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/service
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/storage
)
