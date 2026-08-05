add_library(
  icts_test_module_topology_fast_clustering_fixture_metrics
  STATIC
  ${ICTS_TEST}/module/topology/fast_clustering/fixture/metrics/ClusterGeometryMetrics.cc
)

target_link_libraries(
  icts_test_module_topology_fast_clustering_fixture_metrics
  PUBLIC
  icts_test_base
  PRIVATE
  icts_source_module_topology_clustering
)
