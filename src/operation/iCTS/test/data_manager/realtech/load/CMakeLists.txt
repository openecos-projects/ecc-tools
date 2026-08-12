add_library(
  icts_test_data_manager_realtech_load_factory
  STATIC
  ${ICTS_TEST}/data_manager/realtech/load/RealTechLoadFactory.cc
)

target_link_libraries(
  icts_test_data_manager_realtech_load_factory
  PUBLIC
  icts_test_data_manager_design_fixture_data
  PRIVATE
  icts_test_realtech_base
)
