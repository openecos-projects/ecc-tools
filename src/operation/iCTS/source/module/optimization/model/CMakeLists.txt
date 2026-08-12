add_library(icts_source_module_optimization_model INTERFACE)

target_link_libraries(
  icts_source_module_optimization_model
  INTERFACE
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_routing
)

target_include_directories(
  icts_source_module_optimization_model
  INTERFACE
  ${ICTS_MODULE}
)
