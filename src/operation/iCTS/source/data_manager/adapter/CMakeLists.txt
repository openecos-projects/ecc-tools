set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA ${ICTS_DATA_MANAGER}/adapter/fast_sta)
set(ICTS_DATA_MANAGER_ADAPTER_SDC ${ICTS_DATA_MANAGER}/adapter/sdc)

add_subdirectory(${ICTS_DATA_MANAGER_ADAPTER_FAST_STA})
add_subdirectory(${ICTS_DATA_MANAGER_ADAPTER_SDC})

add_library(icts_source_data_manager_adapter INTERFACE)

target_link_libraries(
  icts_source_data_manager_adapter
  INTERFACE
  icts_source_data_manager_adapter_fast_sta
  icts_source_data_manager_adapter_sdc
)

target_include_directories(icts_source_data_manager_adapter INTERFACE ${ICTS_DATA_MANAGER}/adapter)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_ADAPTER)
  if(MSVC)
    target_compile_options(icts_source_data_manager_adapter INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_data_manager_adapter INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
