add_library(icts_source_toolkit_geometry INTERFACE)
target_sources(icts_source_toolkit_geometry INTERFACE ${ICTS_TOOLKIT_GEOMETRY}/Geometry.hh)

target_link_libraries(icts_source_toolkit_geometry INTERFACE icts_source_data_manager_spatial)

target_include_directories(
  icts_source_toolkit_geometry
  INTERFACE
  ${ICTS_TOOLKIT_GEOMETRY}
  ${ICTS_TOOLKIT}
)

if(DEBUG_ICTS_SOURCE_TOOLKIT_GEOMETRY)
  if(MSVC)
    target_compile_options(icts_source_toolkit_geometry INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_toolkit_geometry INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
