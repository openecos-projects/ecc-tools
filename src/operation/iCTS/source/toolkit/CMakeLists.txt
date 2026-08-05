set(ICTS_TOOLKIT_GEOMETRY ${ICTS_TOOLKIT}/geometry)
set(ICTS_TOOLKIT_LOGGER ${ICTS_TOOLKIT}/logger)
set(ICTS_TOOLKIT_MONITOR ${ICTS_TOOLKIT}/monitor)
set(ICTS_TOOLKIT_UTILITY ${ICTS_TOOLKIT}/utility)
set(ICTS_TOOLKIT_VISUALIZATION ${ICTS_TOOLKIT}/visualization)

add_subdirectory(${ICTS_TOOLKIT_GEOMETRY})
add_subdirectory(${ICTS_TOOLKIT_LOGGER})
add_subdirectory(${ICTS_TOOLKIT_MONITOR})
add_subdirectory(${ICTS_TOOLKIT_UTILITY})
add_subdirectory(${ICTS_TOOLKIT_VISUALIZATION})

add_library(icts_source_toolkit INTERFACE)

target_link_libraries(
  icts_source_toolkit
  INTERFACE
  icts_source_toolkit_geometry
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
  icts_source_toolkit_utility
  icts_source_toolkit_visualization
)

target_include_directories(
  icts_source_toolkit
  INTERFACE
  ${ICTS_TOOLKIT}
  ${ICTS_TOOLKIT_GEOMETRY}
  ${ICTS_TOOLKIT_LOGGER}
  ${ICTS_TOOLKIT_MONITOR}
  ${ICTS_TOOLKIT_UTILITY}
  ${ICTS_TOOLKIT_VISUALIZATION}
)

if(DEBUG_ICTS_SOURCE_TOOLKIT)
  if(MSVC)
    target_compile_options(icts_source_toolkit INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_toolkit INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
