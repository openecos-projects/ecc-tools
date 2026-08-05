add_library(
  icts_source_module_synthesis_htree_plan
  ${ICTS_MODULE_SYNTHESIS_HTREE_PLAN}/Plan.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_plan
  PRIVATE
  icts_source_data_manager_spatial
  icts_source_module_characterization
  icts_source_toolkit_geometry
  icts_source_toolkit_logger
)

add_library(
  icts_source_module_synthesis_htree_depth_plan
  ${ICTS_MODULE_SYNTHESIS_HTREE_PLAN}/DepthPlan.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_depth_plan
  PUBLIC
  icts_source_module_synthesis_htree_compensation
  icts_source_module_synthesis_htree_region
  icts_source_module_synthesis_htree_topology_pruning
  PRIVATE
  icts_source_module_synthesis_htree_plan
)

target_include_directories(
  icts_source_module_synthesis_htree_plan
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)

target_include_directories(
  icts_source_module_synthesis_htree_depth_plan
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
