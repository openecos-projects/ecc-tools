add_library(icts_source_module_synthesis_htree_embedding ${ICTS_MODULE_SYNTHESIS_HTREE_EMBEDDING}/Embedding.cc)

target_link_libraries(
  icts_source_module_synthesis_htree_embedding
  PUBLIC
  icts_source_data_manager_io
  PRIVATE
  icts_source_module_synthesis_htree_region
  icts_source_module_synthesis_htree_segment_pruning
  icts_source_data_manager_design
  icts_source_data_manager_spatial
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_htree_embedding
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
