add_library(
  icts_test_module_topology_fixture_analysis
  STATIC
  ${ICTS_TEST}/module/topology/fixture/analysis/TopologyAnalysis.cc
)

target_link_libraries(
  icts_test_module_topology_fixture_analysis
  PUBLIC
  icts_test_base
)
