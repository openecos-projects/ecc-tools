set(IRCX_API_SRC
  RCXAPI.cc
)

add_library(ircx_api ${IRCX_API_SRC})

target_link_libraries(ircx_api
  PRIVATE
    ircx_flow
    log
)

target_include_directories(ircx_api
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
