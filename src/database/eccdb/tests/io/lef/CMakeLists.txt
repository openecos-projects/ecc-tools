# SPDX-License-Identifier: Apache-2.0

find_package(GTest REQUIRED)

add_executable(eccdb_lef_tech_importer_test
  LefTechImporterTest.cpp
)
eccdb_storage_target(eccdb_lef_tech_importer_test)
target_compile_definitions(eccdb_lef_tech_importer_test
  PRIVATE
    ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
)
target_link_libraries(eccdb_lef_tech_importer_test
  PRIVATE
    eccdb_lef_import
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(eccdb_lef_tech_importer_test)

add_executable(eccdb_lef_library_importer_test
  LefLibraryImporterTest.cpp
)
eccdb_storage_target(eccdb_lef_library_importer_test)
target_compile_definitions(eccdb_lef_library_importer_test
  PRIVATE
    ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
)
target_link_libraries(eccdb_lef_library_importer_test
  PRIVATE
    eccdb_lef_import
    GTest::gtest_main
)
gtest_discover_tests(eccdb_lef_library_importer_test)

add_executable(eccdb_lef_tech_exporter_test
  LefTechExporterTest.cpp
)
eccdb_storage_target(eccdb_lef_tech_exporter_test)
target_include_directories(eccdb_lef_tech_exporter_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../differential"
)
target_compile_definitions(eccdb_lef_tech_exporter_test
  PRIVATE
    ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
)
target_link_libraries(eccdb_lef_tech_exporter_test
  PRIVATE
    eccdb_lef_export
    eccdb_lef_import
    GTest::gtest_main
)
gtest_discover_tests(eccdb_lef_tech_exporter_test)

add_executable(eccdb_lef_library_exporter_test
  LefLibraryExporterTest.cpp
)
eccdb_storage_target(eccdb_lef_library_exporter_test)
target_include_directories(eccdb_lef_library_exporter_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../differential"
)
target_compile_definitions(eccdb_lef_library_exporter_test
  PRIVATE
    ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
)
target_link_libraries(eccdb_lef_library_exporter_test
  PRIVATE
    eccdb_lef_export
    eccdb_lef_import
    GTest::gtest_main
)
gtest_discover_tests(eccdb_lef_library_exporter_test)
