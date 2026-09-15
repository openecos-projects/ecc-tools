add_subdirectory(${IRT_MODULE}/drc_engine)
add_subdirectory(${IRT_MODULE}/topo_builder)
add_subdirectory(${IRT_MODULE}/gds_plotter)
add_subdirectory(${IRT_MODULE}/pin_accessor)
add_subdirectory(${IRT_MODULE}/supply_analyzer)
add_subdirectory(${IRT_MODULE}/planar_router)
add_subdirectory(${IRT_MODULE}/layer_assigner)
add_subdirectory(${IRT_MODULE}/track_assigner)
add_subdirectory(${IRT_MODULE}/detailed_router)
add_subdirectory(${IRT_MODULE}/violation_reporter)
add_subdirectory(${IRT_MODULE}/early_router)

add_library(irt_module INTERFACE)

target_link_libraries(irt_module
    INTERFACE
    irt_drc_engine
    irt_topo_builder
    irt_gds_plotter
    irt_pin_accessor
    irt_supply_analyzer
    irt_planar_router
    irt_layer_assigner
    irt_track_assigner
    irt_detailed_router
    irt_violation_reporter
    irt_early_router
)
