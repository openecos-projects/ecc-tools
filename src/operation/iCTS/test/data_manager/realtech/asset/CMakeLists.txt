add_library(
  icts_test_data_manager_realtech_asset_loader
  STATIC
  ${ICTS_TEST}/data_manager/realtech/asset/RealTechAssetLoader.cc
  ${ICTS_TEST}/data_manager/realtech/asset/RealTechPinCapProbe.cc
)

target_link_libraries(
  icts_test_data_manager_realtech_asset_loader
  PRIVATE
  icts_test_data_manager_design_fixture_generated_pins
  icts_test_toolkit_io
  icts_test_realtech_base
)
