add_library(
  icts_source_module_topology_cluster_constraints
  ${ICTS_MODULE_TOPOLOGY}/cluster_constraints/ClusterConstraintEvaluator.cc
)

target_link_libraries(
  icts_source_module_topology_cluster_constraints
  PUBLIC
  icts_source_data_manager_spatial
  PRIVATE
  icts_source_data_manager_design
  icts_source_module_routing
  icts_source_module_routing_helper
  icts_source_module_timing
  icts_source_module_topology_config
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_topology_cluster_constraints
  PUBLIC
  ${ICTS_MODULE_TOPOLOGY}/cluster_constraints
)
