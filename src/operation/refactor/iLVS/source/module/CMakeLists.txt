add_subdirectory(${ILVS_MODULE}/netlist_extractor)
add_subdirectory(${ILVS_MODULE}/snapshot_io)
add_subdirectory(${ILVS_MODULE}/lvs_checker)
add_subdirectory(${ILVS_MODULE}/lvs_reporter)

add_library(ilvs_module INTERFACE)

target_link_libraries(ilvs_module
    INTERFACE
    ilvs_netlist_extractor
    ilvs_snapshot_io
    ilvs_lvs_checker
    ilvs_lvs_reporter
)
