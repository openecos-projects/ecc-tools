add_library(icts_source_data_manager_config ${ICTS_DATA_MANAGER_CONFIG}/Config.cc)

target_link_libraries(
  icts_source_data_manager_config
  PRIVATE
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_config
  BEFORE
  PUBLIC
  ${ICTS_DATA_MANAGER_CONFIG}
  ${ICTS_DATA_MANAGER}
  PRIVATE
  ${HOME_THIRDPARTY}/json
)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_CONFIG)
  if(MSVC)
    target_compile_options(icts_source_data_manager_config PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_data_manager_config PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_data_manager_config PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
