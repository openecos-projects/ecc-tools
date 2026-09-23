add_subdirectory(${IPW_MODULE}/delay_calculator)
add_subdirectory(${IPW_MODULE}/graph_builder)
add_subdirectory(${IPW_MODULE}/clock_propagator)
add_subdirectory(${IPW_MODULE}/timing_propagator)
add_subdirectory(${IPW_MODULE}/power_propagator)
add_subdirectory(${IPW_MODULE}/power_analyzer)
add_subdirectory(${IPW_MODULE}/power_reporter)
add_subdirectory(${IPW_MODULE}/sdc_command)

add_library(ipw_module INTERFACE)

target_link_libraries(ipw_module
    INTERFACE
    ipw_graph_builder
    ipw_delay_calculator
    ipw_clock_propagator
    ipw_timing_propagator
    ipw_power_propagator
    ipw_power_analyzer
    ipw_power_reporter
    ipw_sdc_command
)
