add_library(ircx_flow_report Report.cc)

target_link_libraries(ircx_flow_report
  PUBLIC
    ircx_headers
  PRIVATE
    ircx_config
    ircx_database_design
    ircx_report
    log
)

target_include_directories(ircx_flow_report
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
