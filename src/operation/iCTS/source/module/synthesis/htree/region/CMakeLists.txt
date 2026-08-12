add_library(icts_source_module_synthesis_htree_region ${ICTS_MODULE_SYNTHESIS_HTREE_REGION}/SinkLoadRegion.cc)

target_link_libraries(
  icts_source_module_synthesis_htree_region
  PRIVATE
  icts_source_module_synthesis_htree_segment_pruning
  icts_source_module_synthesis_htree_constraint
  icts_source_data_manager_design
  icts_source_data_manager_routing
  icts_source_data_manager_spatial
  icts_source_module_topology_clustering
  icts_source_module_topology_config
  icts_source_module_topology_fast_clustering
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_htree_region
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
