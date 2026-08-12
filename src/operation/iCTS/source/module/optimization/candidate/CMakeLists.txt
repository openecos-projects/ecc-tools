add_library(
  icts_source_module_optimization_candidate
  ${ICTS_MODULE_OPTIMIZATION_CANDIDATE}/OptimizationCandidates.cc
  ${ICTS_MODULE_OPTIMIZATION_CANDIDATE}/OptimizationScalableCandidates.cc
)

target_link_libraries(
  icts_source_module_optimization_candidate
  PUBLIC
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_optimization_model
  PRIVATE
  icts_source_module_optimization_policy
  icts_source_module_optimization_preparation
  icts_source_module_optimization_state
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_optimization_candidate
  PUBLIC
  ${ICTS_MODULE}
)
