add_library(eccdb_c SHARED eccdb_c.cpp)
add_library(EccDB::c ALIAS eccdb_c)
set_target_properties(eccdb_c PROPERTIES EXPORT_NAME c OUTPUT_NAME eccdb_c VERSION "0.1.0" SOVERSION "0")
if(UNIX AND NOT APPLE)
  set_target_properties(eccdb_c PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
target_compile_features(eccdb_c PRIVATE cxx_std_20)
target_include_directories(eccdb_c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(eccdb_c PRIVATE EccDB::eccdb)

if(ECCDB_BUILD_TESTS)
  enable_language(C)
  enable_testing()
  add_executable(eccdb_c_smoke tests/eccdb_c_smoke.c)
  target_link_libraries(eccdb_c_smoke PRIVATE EccDB::c)
  add_test(NAME eccdb_c_smoke COMMAND eccdb_c_smoke)
  if(UNIX AND NOT APPLE)
    add_test(NAME eccdb_c_symbols COMMAND "${CMAKE_COMMAND}"
      "-DNM=${CMAKE_NM}"
      "-DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/eccdb_c/eccdb.h"
      "-DLIBRARY=$<TARGET_FILE:eccdb_c>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/symbols.cmake")
  endif()
  add_executable(eccdb_c_fixture tests/fixture.cpp)
  add_executable(eccdb_c_conversions tests/conversions.cpp)
  target_include_directories(eccdb_c_conversions PRIVATE include)
  target_link_libraries(eccdb_c_conversions PRIVATE EccDB::eccdb)
  add_test(NAME eccdb_c_conversions COMMAND eccdb_c_conversions)
  target_include_directories(eccdb_c_fixture PRIVATE "${ECCDB_SOURCE_DIR}")
  target_link_libraries(eccdb_c_fixture PRIVATE eccdb_binary_export)
  add_executable(eccdb_c_consumer tests/consumer.c)
  target_link_libraries(eccdb_c_consumer PRIVATE EccDB::c)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-data")
  add_test(NAME eccdb_c_fixture COMMAND eccdb_c_fixture)
  add_test(NAME eccdb_c_consumer COMMAND eccdb_c_consumer)
  set_tests_properties(eccdb_c_fixture PROPERTIES FIXTURES_SETUP c_api_data)
  set_tests_properties(eccdb_c_consumer PROPERTIES FIXTURES_REQUIRED c_api_data)
  set_tests_properties(eccdb_c_fixture eccdb_c_consumer PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-data")
endif()

include(GNUInstallDirs)
install(TARGETS eccdb_c
  EXPORT EccDBTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/eccdb_c DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
