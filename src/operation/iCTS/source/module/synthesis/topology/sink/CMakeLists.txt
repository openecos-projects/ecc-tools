add_library(
  icts_source_module_synthesis_topology_sink
  ${ICTS_MODULE_SYNTHESIS_TOPOLOGY_SINK}/SinkBranch.cc
  ${ICTS_MODULE_SYNTHESIS_TOPOLOGY_SINK}/SinkLoadClustering.cc
)

target_link_libraries(
  icts_source_module_synthesis_topology_sink
  PRIVATE
  icts_source_module_synthesis_htree
  icts_source_module_synthesis_htree_characterization_library
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_data_manager_routing
  icts_source_module_topology_clustering
  icts_source_module_topology_config
  icts_source_module_topology_fast_clustering
  icts_source_module_synthesis_topology_buffer
  icts_source_module_synthesis_trace_distance
  icts_source_module_synthesis_trace_topology_build
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_topology_sink
  PUBLIC
  ${ICTS_MODULE}
)
