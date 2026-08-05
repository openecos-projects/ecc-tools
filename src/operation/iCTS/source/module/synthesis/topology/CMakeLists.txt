set(ICTS_MODULE_SYNTHESIS_TOPOLOGY_BUFFER ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/buffer)
set(ICTS_MODULE_SYNTHESIS_TOPOLOGY_SINK ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/sink)
set(ICTS_MODULE_SYNTHESIS_TOPOLOGY_TRUNK ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/trunk)

add_library(
  icts_source_module_synthesis_topology_source_trunk_stage
  ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/SourceTrunkStage.cc
)

target_include_directories(
  icts_source_module_synthesis_topology_source_trunk_stage
  PUBLIC
  ${ICTS_MODULE}
)

add_subdirectory(${ICTS_MODULE_SYNTHESIS_TOPOLOGY_BUFFER})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_TOPOLOGY_SINK})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_TOPOLOGY_TRUNK})

add_library(
  icts_source_module_synthesis_topology
  ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/Topology.cc
  ${ICTS_MODULE_SYNTHESIS_TOPOLOGY}/layout/ClockLayoutBuilder.cc
)

target_link_libraries(
  icts_source_module_synthesis_topology
  PRIVATE
  icts_source_module_synthesis_topology_sink
  icts_source_module_synthesis_topology_trunk
  icts_source_data_manager_config
  icts_source_data_manager_io
  icts_source_data_manager_spatial
  icts_source_module_routing
  icts_source_module_synthesis_distribution
  icts_source_module_synthesis_realization
  icts_source_module_synthesis_topology_source_trunk_stage
  icts_source_module_synthesis_trace_domain_status
  icts_source_toolkit_geometry
  icts_source_toolkit_logger
)

target_link_libraries(
  icts_source_module_synthesis_topology
  PUBLIC
  icts_source_module_synthesis_htree
  icts_source_module_topology_clustering
)

target_include_directories(
  icts_source_module_synthesis_topology
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
