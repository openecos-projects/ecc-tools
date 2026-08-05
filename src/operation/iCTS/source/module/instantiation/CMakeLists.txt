add_library(
  icts_source_module_instantiation
  ${ICTS_MODULE_INSTANTIATION}/Instantiation.cc
)

target_link_libraries(
  icts_source_module_instantiation
  PUBLIC
  icts_source_data_manager
  PRIVATE
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
)

target_include_directories(
  icts_source_module_instantiation
  PUBLIC
  ${ICTS_MODULE}
)
