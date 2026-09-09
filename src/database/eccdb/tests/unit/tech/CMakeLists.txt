# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)

add_executable(eccdb_cut_layer_test
  CutLayerStorageTest.cpp
)
eccdb_storage_target(eccdb_cut_layer_test)
target_link_libraries(eccdb_cut_layer_test
  PRIVATE
    eccdb_tech_layers
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(eccdb_cut_layer_test)

add_executable(eccdb_routing_layer_test
  RoutingLayerStorageTest.cpp
)
eccdb_storage_target(eccdb_routing_layer_test)
target_link_libraries(eccdb_routing_layer_test
  PRIVATE
    eccdb_tech_layers
    GTest::gtest_main
)

gtest_discover_tests(eccdb_routing_layer_test)

add_executable(eccdb_tech_database_test
  TechStoreStorageTest.cpp
)
eccdb_storage_target(eccdb_tech_database_test)
target_link_libraries(eccdb_tech_database_test
  PRIVATE
    eccdb_tech_database
    GTest::gtest_main
)

gtest_discover_tests(eccdb_tech_database_test)

add_executable(eccdb_non_routing_layer_test
  NonRoutingLayerStorageTest.cpp
)
eccdb_storage_target(eccdb_non_routing_layer_test)
target_link_libraries(eccdb_non_routing_layer_test
  PRIVATE
    eccdb_tech_database
    GTest::gtest_main
)

gtest_discover_tests(eccdb_non_routing_layer_test)
