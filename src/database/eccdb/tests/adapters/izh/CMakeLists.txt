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
# MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(izh_native_adapter_test
  IzhNativeAdapterTest.cpp
)

target_compile_features(izh_native_adapter_test PUBLIC cxx_std_20)

target_link_libraries(izh_native_adapter_test
  PRIVATE
    izh_interface
    izh_source
    idrc_interface
    ecc_feature
    idm
    eccdb_core
    GTest::gtest_main
)

gtest_discover_tests(izh_native_adapter_test
  PROPERTIES
    RUN_SERIAL TRUE
)
