# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory
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

add_executable(ista_native_adapter_test
  StaNativeAdapterTest.cpp
)

target_compile_features(ista_native_adapter_test PUBLIC cxx_std_20)

target_link_libraries(ista_native_adapter_test
  PRIVATE
    ista_interface
    ista_source
    spef
    eccdb_core
    GTest::gtest_main
)

gtest_discover_tests(ista_native_adapter_test
  PROPERTIES
    RUN_SERIAL TRUE
)
