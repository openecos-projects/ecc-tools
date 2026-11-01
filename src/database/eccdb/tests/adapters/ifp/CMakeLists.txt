# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(ifp_native_adapter_test
  IfpNativeAdapterTest.cpp
  IfpIoPortUpdateTest.cpp
)

target_compile_features(ifp_native_adapter_test PUBLIC cxx_std_20)

target_link_libraries(ifp_native_adapter_test
  PRIVATE
    ifp_interface
    eccdb_ifp_adapter
    ifp_source
    idm
    eccdb_core
    eccdb_def_export
    GTest::gtest_main
)

gtest_discover_tests(ifp_native_adapter_test
  PROPERTIES
    RUN_SERIAL TRUE
)

# Each provider runs in a fresh process; both consume the same iFP algorithm.
add_executable(ifp_pair_probe ${CMAKE_SOURCE_DIR}/test/native/ifp_pair_probe.cpp)
target_compile_features(ifp_pair_probe PRIVATE cxx_std_20)
target_link_libraries(ifp_pair_probe PRIVATE ifp_interface eccdb_ifp_adapter ifp_source idm eccdb_database_owner)
set_target_properties(ifp_pair_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
