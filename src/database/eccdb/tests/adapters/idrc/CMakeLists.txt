# SPDX-License-Identifier: MulanPSL-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(idrc_adapter_differential_test
  IdrcAdapterDifferentialTest.cpp
)

target_compile_features(idrc_adapter_differential_test PUBLIC cxx_std_20)

target_link_libraries(idrc_adapter_differential_test
  PRIVATE
    idrc_interface
    idrc_data_manager
    idrc_source
    idm
    ecc_feature
    eccdb_design
    eccdb_tech_database
    eccdb_library
    eccdb_lef_import
    eccdb_def_import
    GTest::gtest_main
)

gtest_discover_tests(idrc_adapter_differential_test
  PROPERTIES
    RUN_SERIAL TRUE
)
