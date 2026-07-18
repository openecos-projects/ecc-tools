set(IRCX_DATABASE_PROCESS_SRC
  ItfBuilder.cpp
  Layer.cpp
  ProcessCorner.cpp
)

add_library(ircx_database_process ${IRCX_DATABASE_PROCESS_SRC})

target_link_libraries(ircx_database_process
  PUBLIC
    ircx_headers
    itf
)

target_include_directories(ircx_database_process
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
