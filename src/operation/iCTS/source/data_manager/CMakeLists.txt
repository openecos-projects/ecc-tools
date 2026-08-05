set(ICTS_DATA_MANAGER_ADAPTER ${ICTS_DATA_MANAGER}/adapter)
set(ICTS_DATA_MANAGER_CHARACTERIZATION ${ICTS_DATA_MANAGER}/characterization)
set(ICTS_DATA_MANAGER_CONFIG ${ICTS_DATA_MANAGER}/config)
set(ICTS_DATA_MANAGER_DESIGN ${ICTS_DATA_MANAGER}/design)
set(ICTS_DATA_MANAGER_IO ${ICTS_DATA_MANAGER}/io)
set(ICTS_DATA_MANAGER_QOR ${ICTS_DATA_MANAGER}/qor)
set(ICTS_DATA_MANAGER_ROUTING ${ICTS_DATA_MANAGER}/routing)
set(ICTS_DATA_MANAGER_SPATIAL ${ICTS_DATA_MANAGER}/spatial)
set(ICTS_DATA_MANAGER_TIMING ${ICTS_DATA_MANAGER}/timing)

add_subdirectory(${ICTS_DATA_MANAGER_ADAPTER})
add_subdirectory(${ICTS_DATA_MANAGER_CHARACTERIZATION})
add_subdirectory(${ICTS_DATA_MANAGER_CONFIG})
add_subdirectory(${ICTS_DATA_MANAGER_DESIGN})
add_subdirectory(${ICTS_DATA_MANAGER_IO})
add_subdirectory(${ICTS_DATA_MANAGER_QOR})
add_subdirectory(${ICTS_DATA_MANAGER_ROUTING})
add_subdirectory(${ICTS_DATA_MANAGER_SPATIAL})
add_subdirectory(${ICTS_DATA_MANAGER_TIMING})

add_library(icts_source_data_manager ${ICTS_DATA_MANAGER}/DataManager.cc)

target_sources(
  icts_source_data_manager
  PUBLIC
  ${ICTS_DATA_MANAGER}/DataManager.hh
  ${ICTS_DATA_MANAGER}/stage/StageSummary.hh
)

target_include_directories(
  icts_source_data_manager
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_DATA_MANAGER}
  ${ICTS_DATA_MANAGER_ADAPTER}
  ${ICTS_DATA_MANAGER_CHARACTERIZATION}
  ${ICTS_DATA_MANAGER_CONFIG}
  ${ICTS_DATA_MANAGER_DESIGN}
  ${ICTS_DATA_MANAGER_IO}
  ${ICTS_DATA_MANAGER_QOR}
  ${ICTS_DATA_MANAGER_ROUTING}
  ${ICTS_DATA_MANAGER_SPATIAL}
  ${ICTS_DATA_MANAGER_TIMING}
)

target_link_libraries(
  icts_source_data_manager
  PUBLIC
  icts_source_data_manager_adapter
  icts_source_data_manager_characterization
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_data_manager_io
  icts_source_data_manager_qor
  icts_source_data_manager_routing
  icts_source_data_manager_spatial
  icts_source_data_manager_timing
  PRIVATE
  icts_source_toolkit_logger
  icts_source_toolkit_monitor
  idm
)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER)
  if(MSVC)
    target_compile_options(icts_source_data_manager PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_data_manager PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_data_manager PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
