# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory

# This library is the single native scripting boundary used by both the Python
# and Tcl frontends.  It intentionally depends on existing Operation adapters
# rather than defining a second database or runner model.
add_library(eccdb_scripting EccdbCommands.cpp)
target_compile_features(eccdb_scripting PUBLIC cxx_std_20)
target_include_directories(eccdb_scripting
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/database/eccdb
)
target_link_libraries(eccdb_scripting
  PUBLIC
    eccdb_database_owner
  PRIVATE
    ifp_interface
    icts_interface
    irt_interface
    idrc_interface
    eccdb_adapters
    ista_interface
    ipw_interface
    iemir_interface
    imj_interface
    ilvs_interface
    ilvs_source
    eccdb_verilog_import
    liberty
    spef
    vcd
)
