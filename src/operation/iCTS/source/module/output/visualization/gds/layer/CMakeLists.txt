add_library(icts_source_module_output_visualization_gds_layer ${ICTS_MODULE_OUTPUT_VISUALIZATION_GDS_LAYER}/LayerPolicy.cc)

target_link_libraries(
  icts_source_module_output_visualization_gds_layer
  PUBLIC
  icts_source_module_output_visualization_gds_writer
  PRIVATE
  icts_source_module_output_visualization_drawing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_output_visualization_gds_layer
  PUBLIC
  ${ICTS_MODULE}
)
