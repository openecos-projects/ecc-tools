add_library(ipw_clock_propagator
    ${IPW_MODULE}/clock_propagator/ClockPropagator.cpp
)

target_link_libraries(ipw_clock_propagator
    PUBLIC
    ipw_delay_calculator
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_clock_propagator
    PUBLIC
    ${IPW_MODULE}/clock_propagator/cp_data_manager
    ${IPW_MODULE}/clock_propagator
)
