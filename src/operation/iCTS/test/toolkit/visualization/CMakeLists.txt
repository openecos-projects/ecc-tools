add_library(
  icts_test_toolkit_visualization
  STATIC
  ${ICTS_TEST}/toolkit/visualization/TestVisualization.cc
)

target_link_libraries(
  icts_test_toolkit_visualization
  PRIVATE
  icts_test_base
  icts_source_toolkit_visualization
)
