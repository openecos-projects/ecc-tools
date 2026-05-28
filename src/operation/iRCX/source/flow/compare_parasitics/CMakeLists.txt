add_library(ircx_flow_compare_parasitics
  CompareParasiticsFlow.cc
)

target_link_libraries(ircx_flow_compare_parasitics
  PUBLIC
    ircx_compare_parasitics
    ircx_headers
  PRIVATE
    log
    spef
)

target_include_directories(ircx_flow_compare_parasitics
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
