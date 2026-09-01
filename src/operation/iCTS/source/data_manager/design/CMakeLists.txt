add_library(
  icts_source_data_manager_design
  ${ICTS_DATA_MANAGER_DESIGN}/Clock.cc
  ${ICTS_DATA_MANAGER_DESIGN}/ClockDAG.cc
  ${ICTS_DATA_MANAGER_DESIGN}/ClockLayout.cc
  ${ICTS_DATA_MANAGER_DESIGN}/Design.cc
)

target_link_libraries(
  icts_source_data_manager_design
  PUBLIC
  icts_source_data_manager_spatial
  PRIVATE
  icts_source_data_manager_config
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_design
  PUBLIC
  ${ICTS_DATA_MANAGER_DESIGN}
  ${ICTS_DATA_MANAGER}
  PRIVATE
  ${ICTS_SOURCE}
)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_DESIGN)
  if(MSVC)
    target_compile_options(icts_source_data_manager_design PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_data_manager_design PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_data_manager_design PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
