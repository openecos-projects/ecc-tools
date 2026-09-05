# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(eccdb_geometry_pool_test
  GeometryPoolTest.cpp
)
eccdb_storage_target(eccdb_geometry_pool_test)
target_link_libraries(eccdb_geometry_pool_test
  PRIVATE
    eccdb_geometry
    GTest::gtest_main
)

gtest_discover_tests(eccdb_geometry_pool_test)
