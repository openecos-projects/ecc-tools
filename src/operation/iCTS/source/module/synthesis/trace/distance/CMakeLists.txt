add_library(icts_source_module_synthesis_trace_distance ${ICTS_MODULE_SYNTHESIS_TRACE_DISTANCE}/TopologyDistance.cc)

target_link_libraries(
  icts_source_module_synthesis_trace_distance
  PUBLIC
  icts_source_module_characterization_headers
  PRIVATE
  icts_source_module_synthesis_topology_buffer
  icts_source_module_topology_clustering
  icts_source_data_manager_design
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_trace_distance
  PUBLIC
  ${ICTS_MODULE}
)
