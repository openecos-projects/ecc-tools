add_subdirectory(${IRCX_TOOLKIT}/logger)
add_subdirectory(${IRCX_TOOLKIT}/monitor)
add_subdirectory(${IRCX_TOOLKIT}/utility)

add_library(ircx_toolkit INTERFACE)

target_link_libraries(ircx_toolkit
    INTERFACE
    ircx_logger
    ircx_monitor
    ircx_utility
    ircx_interface
)

target_include_directories(ircx_toolkit
    INTERFACE
    ${IRCX_TOOLKIT}
)
