# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(eccdb_library_site_cell_master_test
  LibraryStoreTest.cpp
)
target_compile_features(eccdb_library_site_cell_master_test PRIVATE cxx_std_20)
target_link_libraries(eccdb_library_site_cell_master_test
  PRIVATE
    eccdb_library
    GTest::gtest_main
)

gtest_discover_tests(eccdb_library_site_cell_master_test)
