add_library(icts_source_module_synthesis_htree_segment_pruning ${ICTS_MODULE_SYNTHESIS_HTREE_SEGMENT_PRUNING}/SegmentPruning.cc)

target_link_libraries(
  icts_source_module_synthesis_htree_segment_pruning
  PUBLIC
  icts_source_module_characterization
  icts_source_toolkit_logger
  PRIVATE
)

target_include_directories(
  icts_source_module_synthesis_htree_segment_pruning
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
