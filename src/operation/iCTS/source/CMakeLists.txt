set(ICTS_DATA_MANAGER ${ICTS_SOURCE}/data_manager)
set(ICTS_MODULE ${ICTS_SOURCE}/module)
set(ICTS_TOOLKIT ${ICTS_SOURCE}/toolkit)

add_subdirectory(${ICTS_DATA_MANAGER})
add_subdirectory(${ICTS_MODULE})
add_subdirectory(${ICTS_TOOLKIT})

add_library(icts_source INTERFACE)

target_link_libraries(
  icts_source
  INTERFACE
  icts_source_module
  icts_source_data_manager
  icts_source_toolkit
)

target_include_directories(icts_source INTERFACE ${ICTS_SOURCE})

if(DEBUG_ICTS_SOURCE)
  if(MSVC)
    target_compile_options(icts_source INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
