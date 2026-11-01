# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Beijing Institute of Open Source Chip

# The providers run in separate processes because iDB, EccDB, and iPW retain
# process-global state.  Keep this diagnostic executable opt-in, but make its
# EccDB ownership explicit rather than depending on a cross-tool flow runner.
add_executable(ipw_pair_probe EXCLUDE_FROM_ALL
  ${CMAKE_SOURCE_DIR}/test/native/ipw_pair_probe.cpp
)
target_compile_features(ipw_pair_probe PRIVATE cxx_std_20)
target_include_directories(ipw_pair_probe BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/src/operation/iPW/source/data_manager
  ${CMAKE_SOURCE_DIR}/src/operation/iPW/source/data_manager/advance
)
target_link_libraries(ipw_pair_probe PRIVATE
  ipw_interface
  eccdb_ipw_adapter
  ipw_source
  eccdb_database_owner
  idm
  liberty
  spef
  vcd
)
set_target_properties(ipw_pair_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
