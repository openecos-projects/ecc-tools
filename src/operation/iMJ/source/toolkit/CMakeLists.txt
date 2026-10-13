add_subdirectory(${IMJ_TOOLKIT}/logger)
add_subdirectory(${IMJ_TOOLKIT}/monitor)
add_subdirectory(${IMJ_TOOLKIT}/utility)

add_library(imj_toolkit INTERFACE)

target_link_libraries(imj_toolkit
    INTERFACE
    imj_logger
    imj_monitor
    imj_utility
    imj_interface
)

target_include_directories(imj_toolkit
    INTERFACE
    ${IMJ_TOOLKIT}
    ${IMJ_DATA_MANAGER}
    ${IMJ_DATA_MANAGER}/advance
)
