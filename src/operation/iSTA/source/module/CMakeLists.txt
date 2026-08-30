add_subdirectory(${ISTA_MODULE}/delay_calculator)
add_subdirectory(${ISTA_MODULE}/graph_builder)
add_subdirectory(${ISTA_MODULE}/clock_propagator)
add_subdirectory(${ISTA_MODULE}/timing_propagator)
add_subdirectory(${ISTA_MODULE}/power_propagator)
add_subdirectory(${ISTA_MODULE}/timing_analyzer)
add_subdirectory(${ISTA_MODULE}/power_analyzer)
add_subdirectory(${ISTA_MODULE}/timing_characterizer)
add_subdirectory(${ISTA_MODULE}/timing_reporter)
add_subdirectory(${ISTA_MODULE}/power_reporter)
add_subdirectory(${ISTA_MODULE}/sdf_writer)
add_subdirectory(${ISTA_MODULE}/sdc_command)

add_library(ista_module INTERFACE)

target_link_libraries(ista_module
    INTERFACE
    ista_graph_builder
    ista_delay_calculator
    ista_clock_propagator
    ista_timing_propagator
    ista_power_propagator
    ista_timing_analyzer
    ista_power_analyzer
    ista_timing_characterizer
    ista_timing_reporter
    ista_power_reporter
    ista_sdf_writer
    ista_sdc_command
)
