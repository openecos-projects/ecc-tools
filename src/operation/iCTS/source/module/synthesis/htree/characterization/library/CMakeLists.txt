add_library(
  icts_source_module_synthesis_htree_characterization_library
  ${ICTS_MODULE_SYNTHESIS_HTREE_CHARACTERIZATION_LIBRARY}/CharacterizationLibrary.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_characterization_library
  PUBLIC
  icts_source_data_manager_routing
  icts_source_module_characterization
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_io
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_htree_characterization_library
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
