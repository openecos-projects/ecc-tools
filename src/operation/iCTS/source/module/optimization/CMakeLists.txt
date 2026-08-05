set(ICTS_MODULE_OPTIMIZATION_CANDIDATE ${ICTS_MODULE_OPTIMIZATION}/candidate)
set(ICTS_MODULE_OPTIMIZATION_MODEL ${ICTS_MODULE_OPTIMIZATION}/model)
set(ICTS_MODULE_OPTIMIZATION_CLOCK_SIZING_EDIT ${ICTS_MODULE_OPTIMIZATION}/clock_sizing_edit)
set(ICTS_MODULE_OPTIMIZATION_POLICY ${ICTS_MODULE_OPTIMIZATION}/policy)
set(ICTS_MODULE_OPTIMIZATION_PREPARATION ${ICTS_MODULE_OPTIMIZATION}/preparation)
set(ICTS_MODULE_OPTIMIZATION_SOLVER ${ICTS_MODULE_OPTIMIZATION}/solver)
set(ICTS_MODULE_OPTIMIZATION_STATE ${ICTS_MODULE_OPTIMIZATION}/state)

add_subdirectory(${ICTS_MODULE_OPTIMIZATION_MODEL})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_POLICY})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_PREPARATION})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_CANDIDATE})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_STATE})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_SOLVER})
add_subdirectory(${ICTS_MODULE_OPTIMIZATION_CLOCK_SIZING_EDIT})

add_library(
  icts_source_module_optimization
  ${ICTS_MODULE_OPTIMIZATION}/Optimization.cc
)

target_link_libraries(
  icts_source_module_optimization
  PUBLIC
  icts_source_data_manager
  PRIVATE
  icts_source_data_manager_adapter_fast_sta
  icts_source_module_routing
  icts_source_module_optimization_model
  icts_source_module_optimization_clock_sizing_edit
  icts_source_module_optimization_policy
  icts_source_module_optimization_preparation
  icts_source_module_optimization_solver
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
  icts_source_toolkit_utility
)

target_include_directories(
  icts_source_module_optimization
  PUBLIC
  ${ICTS_MODULE}
)

if(DEBUG_ICTS_SOURCE_MODULE_OPTIMIZATION)
  if(MSVC)
    target_compile_options(icts_source_module_optimization PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_optimization PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_optimization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
