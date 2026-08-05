add_library(
  icts_test_module_characterization_fixture
  STATIC
  ${ICTS_TEST}/module/characterization/fixture/CharacterizationRealTechFrontierBuilder.cc
  ${ICTS_TEST}/module/characterization/fixture/CharacterizationRealTechFixture.cc
)

target_link_libraries(
  icts_test_module_characterization_fixture
  PUBLIC
    icts_test_base
  PRIVATE
    icts_test_toolkit_io
    icts_test_data_manager_realtech_setup
)
