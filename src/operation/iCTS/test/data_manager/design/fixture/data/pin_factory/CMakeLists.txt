add_library(
  icts_test_data_manager_design_fixture_data_pin_factory
  STATIC
  ${ICTS_TEST}/data_manager/design/fixture/data/pin_factory/PinFactory.cc
)

target_link_libraries(
  icts_test_data_manager_design_fixture_data_pin_factory
  PUBLIC
  icts_test_data_manager_design_fixture_generated_pins
)
