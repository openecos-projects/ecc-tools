# Focused tests for the native unified Geometry proxy and persistence.
# The broader design, DEF and binary suites are separate targets.
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(eccdb_unified_geometry_test
  UnifiedGeometryTest.cpp
  GeometryPayloadServiceTest.cpp
  GeometryOccurrenceServiceTest.cpp
  GeometryEditServiceTest.cpp
  GeometryQueryFilterTest.cpp
  DesignModelServiceTest.cpp
)
target_compile_features(eccdb_unified_geometry_test PRIVATE cxx_std_20)
target_include_directories(eccdb_unified_geometry_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../unit/design"
)
target_link_libraries(eccdb_unified_geometry_test
  PRIVATE
    eccdb_design
    eccdb_service
    eccdb_api
    GTest::gtest_main
)

gtest_discover_tests(eccdb_unified_geometry_test)

if(TARGET def AND TARGET lef AND TARGET eccdb_def_import AND TARGET eccdb_lef_import)
  add_executable(eccdb_geometry_persistence_test
    GeometryPersistenceTest.cpp
  )
  eccdb_storage_target(eccdb_geometry_persistence_test)
  target_link_libraries(eccdb_geometry_persistence_test
    PRIVATE
      eccdb_binary_export
      eccdb_binary_import
      eccdb_def_export
      eccdb_def_import
      eccdb_lef_import
      GTest::gtest_main
  )
  gtest_discover_tests(eccdb_geometry_persistence_test)
endif()
