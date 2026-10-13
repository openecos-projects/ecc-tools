add_subdirectory(${IPW_TOOLKIT}/logger)
add_subdirectory(${IPW_TOOLKIT}/monitor)
add_subdirectory(${IPW_TOOLKIT}/utility)

add_library(ipw_toolkit INTERFACE)

target_link_libraries(ipw_toolkit
    INTERFACE
    ipw_logger
    ipw_monitor
    ipw_utility
    ipw_interface
)

target_include_directories(ipw_toolkit
    INTERFACE
    ${IPW_TOOLKIT}
)
