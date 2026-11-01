# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(ista_native_adapter_test
  StaNativeAdapterTest.cpp
)

target_compile_features(ista_native_adapter_test PUBLIC cxx_std_20)

target_link_libraries(ista_native_adapter_test
  PRIVATE
    ista_interface
    eccdb_ista_adapter
    ista_data_manager
    ista_reader
    spef
    eccdb_core
    GTest::gtest_main
)

gtest_discover_tests(ista_native_adapter_test
  PROPERTIES
    RUN_SERIAL TRUE
)
