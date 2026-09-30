# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

set(_eccdb_design_test_sources
  DatabaseApiTest.cpp
  DesignFillStorageTest.cpp
  DesignFloorplanStorageTest.cpp
  DesignGuideStorageTest.cpp
  DesignNetlistStorageTest.cpp
  DesignPinAccessStorageTest.cpp
  DesignViaServiceTest.cpp
  NetRouteServiceTest.cpp
  PhysicalDesignServiceTest.cpp
  RoutingContextServiceTest.cpp
  DesignRoutingConstraintStorageTest.cpp
)
if(TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  list(APPEND _eccdb_design_test_sources DatabaseOpenTest.cpp)
endif()

add_executable(eccdb_design_test ${_eccdb_design_test_sources})
target_compile_features(eccdb_design_test PRIVATE cxx_std_20)
target_link_libraries(eccdb_design_test
  PRIVATE
    eccdb_api
    eccdb_design
    eccdb_service
    GTest::gtest_main
)

gtest_discover_tests(eccdb_design_test)
