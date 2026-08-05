add_library(icts_source_module_output_export ${ICTS_MODULE_OUTPUT_EXPORT}/OutputPathResolver.cc)

target_link_libraries(
  icts_source_module_output_export
  PRIVATE
  icts_source_data_manager_config
)

target_include_directories(
  icts_source_module_output_export
  PUBLIC
  ${ICTS_MODULE}
)
