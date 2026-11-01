# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

add_library(eccdb_idrc_adapter_test_support STATIC
  IdrcWrappedDatabaseCompare.cpp
)

target_compile_features(eccdb_idrc_adapter_test_support PUBLIC cxx_std_20)

target_link_libraries(eccdb_idrc_adapter_test_support
  PRIVATE
    idrc_interface
    idrc_data_manager
    idrc_source
)

target_include_directories(eccdb_idrc_adapter_test_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(idrc_adapter_differential_test
  IdrcAdapterDifferentialTest.cpp
  IdrcMacroObsTest.cpp
)

target_compile_features(idrc_adapter_differential_test PUBLIC cxx_std_20)

target_link_libraries(idrc_adapter_differential_test
  PRIVATE
    idrc_interface
    eccdb_idrc_adapter
    eccdb_idrc_adapter_test_support
    idrc_data_manager
    idrc_source
    idm
    ecc_feature
    eccdb_design
    eccdb_context
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
