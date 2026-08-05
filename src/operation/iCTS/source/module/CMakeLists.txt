set(ICTS_MODULE_ANALYTICAL_CHARACTERIZATION ${ICTS_MODULE}/analytical_characterization)
set(ICTS_MODULE_CHARACTERIZATION ${ICTS_MODULE}/characterization)
set(ICTS_MODULE_EVALUATION ${ICTS_MODULE}/evaluation)
set(ICTS_MODULE_INSTANTIATION ${ICTS_MODULE}/instantiation)
set(ICTS_MODULE_OPTIMIZATION ${ICTS_MODULE}/optimization)
set(ICTS_MODULE_OUTPUT ${ICTS_MODULE}/output)
set(ICTS_MODULE_ROUTING ${ICTS_MODULE}/routing)
set(ICTS_MODULE_SYNTHESIS ${ICTS_MODULE}/synthesis)
set(ICTS_MODULE_TIMING ${ICTS_MODULE}/timing)
set(ICTS_MODULE_TOPOLOGY ${ICTS_MODULE}/topology)

add_subdirectory(${ICTS_MODULE_CHARACTERIZATION})
add_subdirectory(${ICTS_MODULE_ANALYTICAL_CHARACTERIZATION})
add_subdirectory(${ICTS_MODULE_ROUTING})
add_subdirectory(${ICTS_MODULE_TIMING})
add_subdirectory(${ICTS_MODULE_TOPOLOGY})
add_subdirectory(${ICTS_MODULE_SYNTHESIS})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION})
add_subdirectory(${ICTS_MODULE_INSTANTIATION})
add_subdirectory(${ICTS_MODULE_EVALUATION})
add_subdirectory(${ICTS_MODULE_OUTPUT})

add_library(icts_source_module INTERFACE)

target_link_libraries(
  icts_source_module
  INTERFACE
  icts_source_module_evaluation
  icts_source_module_instantiation
  icts_source_module_optimization
  icts_source_module_output
  icts_source_module_routing
  icts_source_module_synthesis
  icts_source_module_timing
  icts_source_module_topology
  icts_source_module_analytical_characterization
  icts_source_module_characterization
)

target_include_directories(
  icts_source_module
  INTERFACE
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)

if(DEBUG_ICTS_SOURCE_MODULE)
  if(MSVC)
    target_compile_options(icts_source_module INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
