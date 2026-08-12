add_subdirectory(${IEMIR_TOOLKIT}/logger)
add_subdirectory(${IEMIR_TOOLKIT}/monitor)
add_subdirectory(${IEMIR_TOOLKIT}/utility)

add_library(iemir_toolkit INTERFACE)

target_link_libraries(iemir_toolkit
    INTERFACE
    iemir_logger
    iemir_monitor
    iemir_utility
    iemir_interface
)

target_include_directories(iemir_toolkit
    INTERFACE
    ${IEMIR_TOOLKIT}
)
