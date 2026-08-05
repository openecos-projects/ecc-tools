add_library(
  icts_source_module_optimization_state
  ${ICTS_MODULE_OPTIMIZATION_STATE}/OptimizationState.cc
)

target_link_libraries(
  icts_source_module_optimization_state
  PUBLIC
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_optimization_model
)

target_include_directories(
  icts_source_module_optimization_state
  PUBLIC
  ${ICTS_MODULE}
)
