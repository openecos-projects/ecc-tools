add_library(icts_source_data_manager_spatial INTERFACE)

target_include_directories(icts_source_data_manager_spatial INTERFACE ${ICTS_DATA_MANAGER_SPATIAL} ${ICTS_DATA_MANAGER})

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_SPATIAL)
  if(MSVC)
    target_compile_options(icts_source_data_manager_spatial INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_data_manager_spatial INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
