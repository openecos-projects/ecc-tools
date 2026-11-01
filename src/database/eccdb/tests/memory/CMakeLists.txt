# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

# Memory probes and text-import RSS comparisons live outside semantic tests.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET IdbBuilder AND TARGET eccdb_lef_import)
  find_package(GTest REQUIRED)
  include(GoogleTest)

  if(TARGET eccdb_def_import)
    add_executable(eccdb_def_memory_comparison_test
      DefMemoryComparisonTest.cpp
    )
    eccdb_storage_target(eccdb_def_memory_comparison_test)
    target_compile_definitions(eccdb_def_memory_comparison_test
      PRIVATE
        OPENROAD_SOURCE_DEFAULT="${OPENROAD_SOURCE_DIR}"
    )
    target_link_libraries(eccdb_def_memory_comparison_test
      PRIVATE
        IdbBuilder
        eccdb_lef_import
        eccdb_def_import
        GTest::gtest_main
    )
    gtest_discover_tests(eccdb_def_memory_comparison_test)
  endif()

  add_executable(eccdb_legacy_idb_memory_probe
    LegacyIdbMemoryProbe.cpp
  )
  eccdb_storage_target(eccdb_legacy_idb_memory_probe)
  target_link_libraries(eccdb_legacy_idb_memory_probe
    PRIVATE
      IdbBuilder
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET eccdb_lef_import AND TARGET eccdb_def_import)
  add_executable(eccdb_entt_memory_probe
    EnttMemoryProbe.cpp
  )
  eccdb_storage_target(eccdb_entt_memory_probe)
  target_link_libraries(eccdb_entt_memory_probe
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
  )
endif()
