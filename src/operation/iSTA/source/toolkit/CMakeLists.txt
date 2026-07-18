add_subdirectory(${ISTA_TOOLKIT}/logger)
add_subdirectory(${ISTA_TOOLKIT}/monitor)
add_subdirectory(${ISTA_TOOLKIT}/utility)

add_library(ista_toolkit INTERFACE)

target_link_libraries(ista_toolkit
    INTERFACE
    ista_logger
    ista_monitor
    ista_utility
    ista_interface
)

target_include_directories(ista_toolkit
    INTERFACE
    ${ISTA_TOOLKIT}
)
