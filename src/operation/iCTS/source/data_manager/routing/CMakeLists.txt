add_library(icts_source_data_manager_routing INTERFACE)

target_link_libraries(icts_source_data_manager_routing INTERFACE icts_source_data_manager_spatial)

target_include_directories(icts_source_data_manager_routing INTERFACE ${ICTS_DATA_MANAGER_ROUTING} ${ICTS_DATA_MANAGER})

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_ROUTING)
  if(MSVC)
    target_compile_options(icts_source_data_manager_routing INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_data_manager_routing INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
