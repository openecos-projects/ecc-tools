add_library(ircx_flow INTERFACE)

target_link_libraries(ircx_flow
  INTERFACE
    ircx_flow_extraction
    ircx_flow_report
    ircx_flow_setup
)

target_include_directories(ircx_flow
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
