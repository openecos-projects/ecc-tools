add_subdirectory(${ILVS_TOOLKIT}/logger)
add_subdirectory(${ILVS_TOOLKIT}/monitor)
add_subdirectory(${ILVS_TOOLKIT}/utility)

add_library(ilvs_toolkit INTERFACE)

target_link_libraries(ilvs_toolkit
    INTERFACE
    ilvs_logger
    ilvs_monitor
    ilvs_utility
    ilvs_interface
)

target_include_directories(ilvs_toolkit
    INTERFACE
    ${ILVS_TOOLKIT}
)
