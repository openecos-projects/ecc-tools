add_library(ircx_flow_compare_spef
  CompareSpef.cc
)

target_link_libraries(ircx_flow_compare_spef
  PUBLIC
    ircx_compare_spef
    ircx_headers
  PRIVATE
    log
)

target_include_directories(ircx_flow_compare_spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
