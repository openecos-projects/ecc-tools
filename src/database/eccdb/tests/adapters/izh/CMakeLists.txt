# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(imj_native_adapter_test EXCLUDE_FROM_ALL
  IzhNativeAdapterTest.cpp
)

target_compile_features(imj_native_adapter_test PUBLIC cxx_std_20)

target_link_libraries(imj_native_adapter_test
  PRIVATE
    imj_interface
    eccdb_imj_adapter
    imj_source
    idrc_interface
    ecc_feature
    idm
    eccdb_core
    GTest::gtest_main
)

gtest_discover_tests(imj_native_adapter_test
  PROPERTIES
    RUN_SERIAL TRUE
)

# iMJ's iDB and EccDB providers need separate processes because their legacy
# singleton state cannot coexist in one address space.  This probe owns an
# EccDB Database directly and hands only its Context to iMJ.
add_executable(imj_pair_probe EXCLUDE_FROM_ALL
  ${CMAKE_SOURCE_DIR}/test/native/imj_pair_probe.cpp
)
target_compile_features(imj_pair_probe PRIVATE cxx_std_20)
target_link_libraries(imj_pair_probe PRIVATE
  imj_interface
  eccdb_imj_adapter
  imj_source
  eccdb_database_owner
  idm
)
set_target_properties(imj_pair_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# This exercises the EccDB adaptor's graph-level antenna kernel.
add_executable(imj_antenna_kernel_test EXCLUDE_FROM_ALL
  ${CMAKE_SOURCE_DIR}/test/native/imj_antenna_kernel_test.cpp
)
target_link_libraries(imj_antenna_kernel_test PRIVATE eccdb_imj_adapter imj_data_manager)
target_include_directories(imj_antenna_kernel_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/operation/iMJ/source/module/antenna_checker/ac_data_manager)
set_target_properties(imj_antenna_kernel_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Test the native fill planner without changing the upstream iMJ test source.
add_executable(imj_metal_kernel_test EXCLUDE_FROM_ALL
  ${CMAKE_SOURCE_DIR}/test/native/imj_metal_kernel_test.cpp
)
target_link_libraries(imj_metal_kernel_test PRIVATE eccdb_imj_adapter imj_source)
set_target_properties(imj_metal_kernel_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
