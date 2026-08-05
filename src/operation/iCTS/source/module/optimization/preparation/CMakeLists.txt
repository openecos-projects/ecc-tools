add_library(
  icts_source_module_optimization_preparation
  ${ICTS_MODULE_OPTIMIZATION_PREPARATION}/OptimizationPreparation.cc
)

target_link_libraries(
  icts_source_module_optimization_preparation
  PUBLIC
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_optimization_model
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_module_optimization_policy
  icts_source_module_routing
  icts_source_toolkit_logger
  icts_source_toolkit_utility
)

target_include_directories(
  icts_source_module_optimization_preparation
  PUBLIC
  ${ICTS_MODULE}
)
