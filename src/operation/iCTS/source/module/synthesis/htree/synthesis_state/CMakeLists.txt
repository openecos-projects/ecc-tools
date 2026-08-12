add_library(
  icts_source_module_synthesis_htree_synthesis_state
  ${ICTS_MODULE_SYNTHESIS_HTREE_SYNTHESIS_STATE}/SynthesisState.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_synthesis_state
  PUBLIC
  icts_source_module_synthesis_htree_characterization_library
  icts_source_module_synthesis_htree_characterization
  icts_source_module_synthesis_htree_compensation
  icts_source_module_synthesis_htree_region
  icts_source_module_synthesis_htree_topology_pruning
  PRIVATE
  icts_source_module_synthesis_htree_plan
  icts_source_data_manager_design
  icts_source_module_topology
  icts_source_data_manager_routing
)

target_include_directories(
  icts_source_module_synthesis_htree_synthesis_state
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
