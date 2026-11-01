# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(eccdb_geometry_registry_test
  GeometryRegistryTest.cpp
)
eccdb_storage_target(eccdb_geometry_registry_test)
target_link_libraries(eccdb_geometry_registry_test
  PRIVATE
    eccdb_geometry
    eccdb_entt
    GTest::gtest_main
)

gtest_discover_tests(eccdb_geometry_registry_test)
