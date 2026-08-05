add_library(
  icts_test_data_manager_design_fixture_generated_pins
  STATIC
  ${ICTS_TEST}/data_manager/design/fixture/data/GeneratedPins.cc
)

target_link_libraries(
  icts_test_data_manager_design_fixture_generated_pins
  PUBLIC
  icts_source_data_manager_spatial
  icts_test_base
)

add_subdirectory(pin_factory)
add_subdirectory(distribution)

add_library(
  icts_test_data_manager_design_fixture_data
  STATIC
  ${ICTS_TEST}/data_manager/design/fixture/data/TestDataGenerator.cc
)

target_link_libraries(
  icts_test_data_manager_design_fixture_data
  PUBLIC
  icts_test_data_manager_design_fixture_generated_pins
  PRIVATE
  icts_test_data_manager_design_fixture_data_pin_factory
  icts_test_data_manager_design_fixture_data_distribution
)
