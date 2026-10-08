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

add_executable(eccdb_binary_database_archive_test
  BinaryDatabaseArchiveTest.cpp
)
eccdb_storage_target(eccdb_binary_database_archive_test)
target_include_directories(eccdb_binary_database_archive_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../differential"
)
target_compile_definitions(eccdb_binary_database_archive_test
  PRIVATE
    ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
)
target_link_libraries(eccdb_binary_database_archive_test
  PRIVATE
    eccdb_binary_export
    eccdb_binary_import
    eccdb_lef_export
    eccdb_lef_import
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(eccdb_binary_database_archive_test)

if(TARGET eccdb_def_import AND TARGET eccdb_def_export)
  add_executable(eccdb_design_binary_database_archive_test
    DesignBinaryDatabaseArchiveTest.cpp
  )
  eccdb_storage_target(eccdb_design_binary_database_archive_test)
  target_include_directories(eccdb_design_binary_database_archive_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../differential"
  )
  target_link_libraries(eccdb_design_binary_database_archive_test
    PRIVATE
      eccdb_binary_export
      eccdb_binary_import
      eccdb_def_export
      eccdb_def_import
      eccdb_lef_import
      GTest::gtest_main
  )
  gtest_discover_tests(eccdb_design_binary_database_archive_test)
endif()
