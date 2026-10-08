# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

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
