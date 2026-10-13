# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
# Licensed under Mulan PSL v2. See the project LICENSE for details.
add_library(ecc_native_toolchain NativeToolchain.cpp)
target_compile_features(ecc_native_toolchain PUBLIC cxx_std_20)
target_include_directories(ecc_native_toolchain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecc_native_toolchain
  PUBLIC eccdb_database_owner eccdb_core
  PRIVATE ifp_interface icts_interface irt_interface idrc_interface
          ircx_interface ista_interface ilvs_interface iemir_interface imj_interface
          IdbBuilder ilvs_data_manager liberty spef
)

add_executable(ecc_native_flow native_toolchain_main.cpp)
target_link_libraries(ecc_native_flow PRIVATE ecc_native_toolchain)
set_target_properties(ecc_native_flow PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(ECC_TOOLS_NATIVE_INTEGRATION_TESTS "Run native GCD integration against a local ICS55 PDK" OFF)
set(ECC_TOOLS_NATIVE_PDK_ROOT "" CACHE PATH "Root of the public icsprout55-pdk checkout")
if(BUILD_TESTING AND ECC_TOOLS_NATIVE_INTEGRATION_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  if(NOT IS_DIRECTORY "${ECC_TOOLS_NATIVE_PDK_ROOT}")
    message(FATAL_ERROR "ECC_TOOLS_NATIVE_INTEGRATION_TESTS requires ECC_TOOLS_NATIVE_PDK_ROOT")
  endif()
  add_test(NAME native_gcd_cts_routing_drc
    COMMAND ${CMAKE_COMMAND} -E env
      ECC_TOOLS_TEST_REPO_ROOT=${CMAKE_SOURCE_DIR}
      ECC_TOOLS_TEST_PDK_ROOT=${ECC_TOOLS_NATIVE_PDK_ROOT}
      ECC_TOOLS_TEST_ARTIFACT_ROOT=${CMAKE_BINARY_DIR}/native-test-artifacts
      ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/native/toolchain_scenarios.py
      --binary $<TARGET_FILE:ecc_native_flow> --stages cts routing drc)
  set_tests_properties(native_gcd_cts_routing_drc PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endif()
