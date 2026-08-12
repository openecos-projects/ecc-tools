add_subdirectory(${IZH_TOOLKIT}/logger)
add_subdirectory(${IZH_TOOLKIT}/monitor)
add_subdirectory(${IZH_TOOLKIT}/utility)

add_library(izh_toolkit INTERFACE)

target_link_libraries(izh_toolkit
    INTERFACE
    izh_logger
    izh_monitor
    izh_utility
    izh_interface
)

target_include_directories(izh_toolkit
    INTERFACE
    ${IZH_TOOLKIT}
    ${IZH_TOOLKIT}/header
)
