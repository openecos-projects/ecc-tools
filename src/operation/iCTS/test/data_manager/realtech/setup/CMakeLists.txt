add_library(
  icts_test_data_manager_realtech_setup
  STATIC
  ${ICTS_TEST}/data_manager/realtech/setup/RealTechDesignSetup.cc
)

target_link_libraries(
  icts_test_data_manager_realtech_setup
  PUBLIC
  icts_test_data_manager_design_fixture_generated_pins
  PRIVATE
  icts_source_toolkit_logger
  icts_test_realtech_base
  icts_test_data_manager_realtech_asset_loader
  icts_test_data_manager_realtech_load_factory
)
