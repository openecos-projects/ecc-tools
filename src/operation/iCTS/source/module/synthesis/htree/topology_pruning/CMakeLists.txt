add_library(
  icts_source_module_synthesis_htree_topology_pruning
  ${ICTS_MODULE_SYNTHESIS_HTREE_TOPOLOGY_PRUNING}/TopologyPruning.cc
  ${ICTS_MODULE_SYNTHESIS_HTREE_TOPOLOGY_PRUNING}/TopologyPruningGlobalSelection.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_topology_pruning
  PUBLIC
  icts_source_module_synthesis_htree_constraint
  icts_source_module_synthesis_htree_segment_pruning
  PRIVATE
  icts_source_module_synthesis_htree_compensation
  icts_source_module_synthesis_htree_region
)

target_include_directories(
  icts_source_module_synthesis_htree_topology_pruning
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
