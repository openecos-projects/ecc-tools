add_library(
  icts_source_module_optimization_policy
  ${ICTS_MODULE_OPTIMIZATION_POLICY}/OptimizationPolicy.cc
)

target_include_directories(
  icts_source_module_optimization_policy
  PUBLIC
  ${ICTS_MODULE}
)
