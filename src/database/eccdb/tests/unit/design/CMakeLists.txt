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

set(_eccdb_design_test_sources
  NativeAccessTest.cpp
  ConnectivityServiceTest.cpp
  InstanceServiceTest.cpp
  ConstraintServiceTest.cpp
  FloorplanServiceTest.cpp
  DesignFillStorageTest.cpp
  DesignFloorplanStorageTest.cpp
  DesignGuideStorageTest.cpp
  DesignNetlistStorageTest.cpp
  DesignPinAccessStorageTest.cpp
  DesignRouteShapeTest.cpp
  RoutingViaServiceTest.cpp
  RoutingRouteServiceTest.cpp
  RoutingServiceTest.cpp
  PhysicalDesignServiceTest.cpp
  RoutingPinAccessServiceTest.cpp
  DesignRoutingConstraintStorageTest.cpp
)
if(TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  list(APPEND _eccdb_design_test_sources DatabaseOpenTest.cpp)
endif()
if(TARGET eccdb_verilog_import AND TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  list(APPEND _eccdb_design_test_sources VerilogImportTest.cpp)
endif()

add_executable(eccdb_design_test ${_eccdb_design_test_sources})
target_compile_features(eccdb_design_test PRIVATE cxx_std_20)
target_link_libraries(eccdb_design_test
  PRIVATE
    eccdb_database_owner
    eccdb_core
    GTest::gtest_main
)
target_compile_definitions(eccdb_design_test PRIVATE
  ECCDB_TEST_DATA_ROOT="${ECCDB_TEST_DATA_ROOT}"
)

gtest_discover_tests(eccdb_design_test)
