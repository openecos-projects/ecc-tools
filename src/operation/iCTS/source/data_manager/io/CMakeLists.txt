add_library(
  icts_source_data_manager_io
  ${ICTS_DATA_MANAGER_IO}/Wrapper.cc
  ${ICTS_DATA_MANAGER_IO}/WrapperLiberty.cc
  ${ICTS_DATA_MANAGER_IO}/WrapperRC.cc
  ${ICTS_DATA_MANAGER_IO}/WrapperClockReader.cc
  ${ICTS_DATA_MANAGER_IO}/WrapperClockWriter.cc
)

target_include_directories(
  icts_source_data_manager_io
  PUBLIC
  ${ICTS_DATA_MANAGER_IO}
  ${ICTS_DATA_MANAGER}
  PRIVATE
  ${HOME_DATABASE}/data/design
  ${HOME_DATABASE}/data/design/db_design
  ${HOME_DATABASE}/data/design/db_layout
  ${HOME_DATABASE}/manager/builder
)

target_link_libraries(
  icts_source_data_manager_io
  PUBLIC
  icts_source_data_manager_spatial
  icts_source_data_manager_routing
  PRIVATE
  idm
  icts_source_data_manager_adapter_sdc
  icts_source_data_manager_design
  icts_source_toolkit_logger
  liberty
)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_IO)
  if(MSVC)
    target_compile_options(icts_source_data_manager_io PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_data_manager_io PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_data_manager_io PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
