set(ICTS_MODULE_OUTPUT_VISUALIZATION_DRAWING ${ICTS_MODULE_OUTPUT_VISUALIZATION}/drawing)
set(ICTS_MODULE_OUTPUT_VISUALIZATION_GDS ${ICTS_MODULE_OUTPUT_VISUALIZATION}/gds)
set(ICTS_MODULE_OUTPUT_VISUALIZATION_SVG ${ICTS_MODULE_OUTPUT_VISUALIZATION}/svg)

add_subdirectory(${ICTS_MODULE_OUTPUT_VISUALIZATION_DRAWING})
add_subdirectory(${ICTS_MODULE_OUTPUT_VISUALIZATION_GDS})
add_subdirectory(${ICTS_MODULE_OUTPUT_VISUALIZATION_SVG})

add_library(icts_source_module_output_visualization ${ICTS_MODULE_OUTPUT_VISUALIZATION}/Visualization.cc)

target_link_libraries(
  icts_source_module_output_visualization
  PRIVATE
  icts_source_module_output_visualization_gds
  icts_source_module_output_visualization_svg
)

target_include_directories(
  icts_source_module_output_visualization
  PUBLIC
  ${ICTS_MODULE}
)
