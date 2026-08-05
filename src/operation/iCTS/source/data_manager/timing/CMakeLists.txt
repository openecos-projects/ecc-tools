add_library(icts_source_data_manager_timing INTERFACE)

target_link_libraries(icts_source_data_manager_timing INTERFACE icts_source_data_manager_spatial)

target_include_directories(icts_source_data_manager_timing INTERFACE ${ICTS_DATA_MANAGER_TIMING} ${ICTS_DATA_MANAGER})

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_TIMING)
  if(MSVC)
    target_compile_options(icts_source_data_manager_timing INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_data_manager_timing INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
