icts_add_test_executable(
  icts_test_data_manager
  SOURCES
    ${ICTS_TEST}/data_manager/DataManagerTest.cc
)

add_subdirectory(adapter)
add_subdirectory(config)
add_subdirectory(design)
add_subdirectory(io)
add_subdirectory(realtech)
add_subdirectory(spatial)
