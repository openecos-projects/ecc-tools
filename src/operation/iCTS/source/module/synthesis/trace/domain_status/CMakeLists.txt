add_library(icts_source_module_synthesis_trace_domain_status ${ICTS_MODULE_SYNTHESIS_TRACE_DOMAIN_STATUS}/DomainStatusRecorder.cc)

target_link_libraries(
  icts_source_module_synthesis_trace_domain_status
  PUBLIC
  icts_source_data_manager
)

target_include_directories(
  icts_source_module_synthesis_trace_domain_status
  PUBLIC
  ${ICTS_MODULE}
)
