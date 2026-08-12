add_library(
  icts_source_module_optimization_clock_sizing_edit
  ${ICTS_MODULE_OPTIMIZATION_CLOCK_SIZING_EDIT}/ClockSizingAcceptedEdit.cc
)

target_link_libraries(
  icts_source_module_optimization_clock_sizing_edit
  PUBLIC
  icts_source_module_optimization_model
  PRIVATE
  icts_source_data_manager_design
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_module_optimization_clock_sizing_edit
  PUBLIC
  ${ICTS_MODULE}
)
