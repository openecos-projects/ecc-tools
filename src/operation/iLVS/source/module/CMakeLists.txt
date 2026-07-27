add_subdirectory(${ILVS_MODULE}/entity_checker)
add_subdirectory(${ILVS_MODULE}/routing_checker)
add_subdirectory(${ILVS_MODULE}/pdn_checker)
add_subdirectory(${ILVS_MODULE}/lvs_reporter)

add_library(ilvs_module INTERFACE)

target_link_libraries(ilvs_module
    INTERFACE
    ilvs_entity_checker
    ilvs_routing_checker
    ilvs_pdn_checker
    ilvs_lvs_reporter
)
