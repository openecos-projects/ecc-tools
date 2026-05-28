add_library(ircx_compare_parasitics
  CompareParasitics.cc
)

target_link_libraries(ircx_compare_parasitics
  PUBLIC
    ircx_headers
)

target_include_directories(ircx_compare_parasitics
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
