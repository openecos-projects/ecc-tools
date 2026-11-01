# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(irt_adapter_differential_test
  IrtAdapterDifferentialTest.cpp
  IrtWrappedDatabaseCompare.cpp
)

target_compile_features(irt_adapter_differential_test PUBLIC cxx_std_20)

target_link_libraries(irt_adapter_differential_test
  PRIVATE
    irt_interface
    eccdb_irt_adapter
    irt_data_manager
    irt_source
    idrc_interface
    eccdb_idrc_adapter
    eccdb_idrc_adapter_test_support
    idm
    ecc_feature
    eccdb_design
    eccdb_tech_database
    eccdb_library
    eccdb_lef_import
    eccdb_def_import
    eccdb_def_export
    eccdb_core
    GTest::gtest_main
)

target_include_directories(irt_adapter_differential_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/differential
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/idrc
)

gtest_discover_tests(irt_adapter_differential_test
  PROPERTIES
    RUN_SERIAL TRUE
)
