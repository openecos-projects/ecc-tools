add_library(
  icts_source_module_optimization_solver
  ${ICTS_MODULE_OPTIMIZATION_SOLVER}/OptimizationSolver.cc
)

target_link_libraries(
  icts_source_module_optimization_solver
  PUBLIC
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_optimization_model
  PRIVATE
  icts_source_module_optimization_candidate
  icts_source_module_optimization_policy
  icts_source_module_optimization_state
  icts_source_toolkit_logger
  icts_source_toolkit_utility
)

target_include_directories(
  icts_source_module_optimization_solver
  PUBLIC
  ${ICTS_MODULE}
)
