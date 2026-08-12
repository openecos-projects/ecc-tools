add_library(
  icts_source_module_output_qor
  ${ICTS_MODULE_OUTPUT_QOR}/QORReport.cc
)

target_link_libraries(
  icts_source_module_output_qor
  PRIVATE
  icts_source_data_manager_qor
  icts_source_module_evaluation_qor
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_output_qor
  PUBLIC
  ${ICTS_MODULE}
)
