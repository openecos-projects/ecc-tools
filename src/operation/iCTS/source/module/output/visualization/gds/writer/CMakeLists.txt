add_library(icts_source_module_output_visualization_gds_writer ${ICTS_MODULE_OUTPUT_VISUALIZATION_GDS_WRITER}/GDSStream.cc)

target_link_libraries(
  icts_source_module_output_visualization_gds_writer
  PUBLIC
  icts_source_data_manager_spatial
  PRIVATE
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_output_visualization_gds_writer
  PUBLIC
  ${ICTS_MODULE}
)
