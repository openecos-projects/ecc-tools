add_library(
  icts_source_toolkit_visualization
  STATIC
  ${ICTS_TOOLKIT_VISUALIZATION}/cluster/ClusterSVGWriter.cc
  ${ICTS_TOOLKIT_VISUALIZATION}/topology/TopologySVGWriter.cc
)

target_link_libraries(
  icts_source_toolkit_visualization
  PUBLIC
  icts_source_data_manager_design
  icts_source_data_manager_spatial
)

target_include_directories(
  icts_source_toolkit_visualization
  PUBLIC
  ${ICTS_TOOLKIT_VISUALIZATION}
  ${ICTS_TOOLKIT}
)

if(DEBUG_ICTS_SOURCE_TOOLKIT_VISUALIZATION)
  if(MSVC)
    target_compile_options(icts_source_toolkit_visualization PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_toolkit_visualization PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_toolkit_visualization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
