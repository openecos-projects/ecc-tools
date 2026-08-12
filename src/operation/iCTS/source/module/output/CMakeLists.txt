set(ICTS_MODULE_OUTPUT_EXPORT ${ICTS_MODULE_OUTPUT}/export)
set(ICTS_MODULE_OUTPUT_QOR ${ICTS_MODULE_OUTPUT}/qor)
set(ICTS_MODULE_OUTPUT_VISUALIZATION ${ICTS_MODULE_OUTPUT}/visualization)

add_subdirectory(${ICTS_MODULE_OUTPUT_EXPORT})
add_subdirectory(${ICTS_MODULE_OUTPUT_QOR})
add_subdirectory(${ICTS_MODULE_OUTPUT_VISUALIZATION})

add_library(
  icts_source_module_output
  ${ICTS_MODULE_OUTPUT}/Output.cc
)

target_link_libraries(
  icts_source_module_output
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_data_manager_io
  icts_source_data_manager
  icts_source_module_evaluation
  icts_source_module_output_export
  icts_source_module_output_qor
  icts_source_module_output_visualization
  icts_source_module_output_visualization_drawing
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
)

target_include_directories(
  icts_source_module_output
  PUBLIC
  ${ICTS_MODULE}
)
