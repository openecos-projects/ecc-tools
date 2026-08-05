add_library(
  icts_source_module_evaluation_qor
  ${ICTS_MODULE_EVALUATION_QOR}/QOREvaluation.cc
  ${ICTS_MODULE_EVALUATION_QOR}/QOREvaluationMetrics.cc
)

target_link_libraries(
  icts_source_module_evaluation_qor
  PUBLIC
  icts_source_data_manager_design
  icts_source_data_manager_qor
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_io
  icts_source_module_routing
  icts_source_module_timing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_evaluation_qor
  PUBLIC
  ${ICTS_MODULE}
)
