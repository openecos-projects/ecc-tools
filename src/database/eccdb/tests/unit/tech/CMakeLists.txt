# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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
