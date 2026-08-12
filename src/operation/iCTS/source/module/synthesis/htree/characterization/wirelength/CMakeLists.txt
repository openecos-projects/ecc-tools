add_library(
  icts_source_module_synthesis_htree_characterization_wirelength
  ${ICTS_MODULE_SYNTHESIS_HTREE_CHARACTERIZATION_WIRELENGTH}/WirelengthGrid.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_characterization_wirelength
  PUBLIC
  icts_source_module_characterization
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_spatial
  icts_source_toolkit_geometry
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_htree_characterization_wirelength
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
