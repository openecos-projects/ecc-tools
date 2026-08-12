add_library(
  icts_source_module_synthesis_htree_compensation
  ${ICTS_MODULE_SYNTHESIS_HTREE_COMPENSATION}/RootDriverCompensation.cc
  ${ICTS_MODULE_SYNTHESIS_HTREE_COMPENSATION}/RootDriverCompensationLoad.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_compensation
  PUBLIC
  icts_source_data_manager_characterization
  PRIVATE
  icts_source_data_manager_io
  icts_source_data_manager_design
  icts_source_data_manager_routing
  icts_source_module_synthesis_htree_segment_pruning
  icts_source_module_routing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_htree_compensation
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
