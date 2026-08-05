add_library(
  icts_test_module_topology_gen_case_factory
  STATIC
  ${ICTS_TEST}/module/topology/topology_gen/fixture/case/TopologyGenCaseFactory.cc
)

target_link_libraries(
  icts_test_module_topology_gen_case_factory
  PUBLIC
  icts_test_data_manager_design_fixture_data
)
