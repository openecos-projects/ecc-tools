add_library(icts_source_module_output_visualization_svg ${ICTS_MODULE_OUTPUT_VISUALIZATION_SVG}/SVGVisualization.cc)

target_link_libraries(
  icts_source_module_output_visualization_svg
  PRIVATE
  icts_source_data_manager_config
  icts_source_module_output_visualization_drawing
  icts_source_toolkit_visualization
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_output_visualization_svg
  PUBLIC
  ${ICTS_MODULE}
)
