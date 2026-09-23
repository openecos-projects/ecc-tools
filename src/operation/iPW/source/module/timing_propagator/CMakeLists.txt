add_library(ipw_timing_propagator
    ${IPW_MODULE}/timing_propagator/TimingPropagator.cpp
)

target_link_libraries(ipw_timing_propagator
    PUBLIC
    ipw_delay_calculator
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_timing_propagator
    PUBLIC
    ${IPW_MODULE}/timing_propagator/tp_data_manager
    ${IPW_MODULE}/timing_propagator
)
