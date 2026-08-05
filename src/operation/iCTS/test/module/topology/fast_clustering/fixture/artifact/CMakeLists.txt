add_library(
  icts_test_module_topology_fast_clustering_fixture_artifact
  STATIC
  ${ICTS_TEST}/module/topology/fast_clustering/fixture/artifact/ClusterArtifactWriter.cc
)

target_link_libraries(
  icts_test_module_topology_fast_clustering_fixture_artifact
  PRIVATE
  icts_test_module_topology_fast_clustering_fixture_metrics
  icts_source_module_topology_clustering
)
