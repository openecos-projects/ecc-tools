set(ICTS_MODULE_EVALUATION_QOR ${ICTS_MODULE_EVALUATION}/qor)

add_subdirectory(${ICTS_MODULE_EVALUATION_QOR})

add_library(icts_source_module_evaluation ${ICTS_MODULE_EVALUATION}/Evaluation.cc)

target_link_libraries(
  icts_source_module_evaluation
  PUBLIC
  icts_source_module_evaluation_qor
  PRIVATE
  icts_source_data_manager
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
)

target_include_directories(
  icts_source_module_evaluation
  PUBLIC
  ${ICTS_MODULE}
)
