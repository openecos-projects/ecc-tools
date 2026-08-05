add_library(
  icts_test_data_manager_design_fixture_data_distribution
  STATIC
  ${ICTS_TEST}/data_manager/design/fixture/data/distribution/DistributionGenerators.cc
)

target_link_libraries(
  icts_test_data_manager_design_fixture_data_distribution
  PUBLIC
  icts_test_data_manager_design_fixture_generated_pins
  PRIVATE
  icts_test_data_manager_design_fixture_data_pin_factory
)
