add_library(icts_source_data_manager_characterization INTERFACE)

target_link_libraries(
  icts_source_data_manager_characterization
  INTERFACE
)

target_include_directories(icts_source_data_manager_characterization INTERFACE ${ICTS_DATA_MANAGER_CHARACTERIZATION} ${ICTS_DATA_MANAGER})

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_CHARACTERIZATION)
  if(MSVC)
    target_compile_options(icts_source_data_manager_characterization INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_data_manager_characterization INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
