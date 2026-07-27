add_library(ircx_tool_run_rcx_from_topology
  RunRCXFromTopologyTool.cc
  config/RunRCXFromTopologyConfig.cc
  topology/SpefTopologyBuilder.cc
)

target_link_libraries(ircx_tool_run_rcx_from_topology
  PUBLIC
    ircx_database_design
    ircx_flow_extraction
    ircx_headers
  PRIVATE
    ircx_topology
    log
    spef
)

target_include_directories(ircx_tool_run_rcx_from_topology
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
