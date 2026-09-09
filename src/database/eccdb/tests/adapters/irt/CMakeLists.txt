# SPDX-License-Identifier: MulanPSL-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(irt_adapter_differential_test
  IrtAdapterDifferentialTest.cpp
)

target_compile_features(irt_adapter_differential_test PUBLIC cxx_std_20)

target_link_libraries(irt_adapter_differential_test
  PRIVATE
    irt_interface
    irt_data_manager
    irt_source
    idrc_interface
    idm
    ecc_feature
    eccdb_design
    eccdb_tech_database
    eccdb_library
    eccdb_lef_import
    eccdb_def_import
    eccdb_def_export
    GTest::gtest_main
)

target_include_directories(irt_adapter_differential_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/differential
)

gtest_discover_tests(irt_adapter_differential_test
  PROPERTIES
    RUN_SERIAL TRUE
)
