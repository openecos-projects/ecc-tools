add_library(
  icts_source_module_synthesis_distribution
  ${ICTS_MODULE_SYNTHESIS_DISTRIBUTION}/ClockDistribution.cc
)

target_link_libraries(
  icts_source_module_synthesis_distribution
  PUBLIC
  icts_source_data_manager_design
  PRIVATE
  icts_source_data_manager_io
  icts_source_data_manager_spatial
  icts_source_module_synthesis_realization
  icts_source_module_synthesis_trace_domain_status
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_synthesis_distribution
  PUBLIC
  ${ICTS_MODULE}
)
