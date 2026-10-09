add_library(ipw_signal_propagator
    ${IPW_MODULE}/signal_propagator/SignalPropagator.cpp
)

target_link_libraries(ipw_signal_propagator
    PUBLIC
    ipw_delay_calculator
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_signal_propagator
    PUBLIC
    ${IPW_MODULE}/signal_propagator
)
