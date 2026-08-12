set(ICTS_MODULE_SYNTHESIS_DISTRIBUTION ${ICTS_MODULE_SYNTHESIS}/distribution)
set(ICTS_MODULE_SYNTHESIS_HTREE ${ICTS_MODULE_SYNTHESIS}/htree)
set(ICTS_MODULE_SYNTHESIS_REALIZATION ${ICTS_MODULE_SYNTHESIS}/realization)
set(ICTS_MODULE_SYNTHESIS_TOPOLOGY ${ICTS_MODULE_SYNTHESIS}/topology)
set(ICTS_MODULE_SYNTHESIS_TRACE ${ICTS_MODULE_SYNTHESIS}/trace)

add_subdirectory(${ICTS_MODULE_SYNTHESIS_REALIZATION})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_TRACE})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_HTREE})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_DISTRIBUTION})
add_subdirectory(${ICTS_MODULE_SYNTHESIS_TOPOLOGY})

add_library(icts_source_module_synthesis ${ICTS_MODULE_SYNTHESIS}/Synthesis.cc)

target_link_libraries(
  icts_source_module_synthesis
  PUBLIC
  icts_source_module_synthesis_distribution
  icts_source_module_synthesis_topology
  icts_source_module_synthesis_trace_domain_status
  PRIVATE
  icts_source_module_synthesis_htree_characterization_library
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
)

target_include_directories(
  icts_source_module_synthesis
  PUBLIC
  ${ICTS_MODULE}
)
