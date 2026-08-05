add_subdirectory(${IFP_TOOLKIT}/logger)
add_subdirectory(${IFP_TOOLKIT}/monitor)
add_subdirectory(${IFP_TOOLKIT}/utility)

add_library(ifp_toolkit INTERFACE)

target_link_libraries(ifp_toolkit
    INTERFACE
    ifp_logger
    ifp_monitor
    ifp_utility
    ifp_interface
)

target_include_directories(ifp_toolkit
    INTERFACE
    ${IFP_TOOLKIT}
)
