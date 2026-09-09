# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(eccdb_def_design_roundtrip_test
  DefDesignRoundTripTest.cpp
)
eccdb_storage_target(eccdb_def_design_roundtrip_test)
target_link_libraries(eccdb_def_design_roundtrip_test
  PRIVATE
    eccdb_def_import
    eccdb_def_export
    GTest::gtest_main
)

gtest_discover_tests(eccdb_def_design_roundtrip_test)
