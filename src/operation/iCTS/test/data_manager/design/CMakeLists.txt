add_subdirectory(fixture)

icts_add_test_executable(
  icts_test_data_manager_design
  SOURCES
    ${ICTS_TEST}/data_manager/design/ClockDAGTest.cc
)
