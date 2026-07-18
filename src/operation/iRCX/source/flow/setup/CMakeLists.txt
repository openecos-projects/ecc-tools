add_library(ircx_flow_setup Setup.cc)

target_link_libraries(ircx_flow_setup
  PUBLIC
    ircx_cap_table
    ircx_config
    ircx_database_design
    ircx_headers
    ircx_mapping
  PRIVATE
    idm
    ircx_database_io
    log
)

target_include_directories(ircx_flow_setup
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
