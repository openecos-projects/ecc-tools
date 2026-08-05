add_library(icts_source_module_output_visualization_drawing ${ICTS_MODULE_OUTPUT_VISUALIZATION_DRAWING}/Drawing.cc)

target_link_libraries(
  icts_source_module_output_visualization_drawing
  PUBLIC
  icts_source_data_manager_design
  icts_source_data_manager_spatial
  PRIVATE
  icts_source_data_manager_io
  icts_source_module_routing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_output_visualization_drawing
  PUBLIC
  ${ICTS_MODULE}
)
