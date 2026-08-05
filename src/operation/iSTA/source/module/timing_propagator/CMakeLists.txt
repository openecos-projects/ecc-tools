add_library(ista_timing_propagator
    ${ISTA_MODULE}/timing_propagator/TimingPropagator.cpp
)

target_link_libraries(ista_timing_propagator
    PUBLIC
    ista_delay_calculator
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_timing_propagator
    PUBLIC
    ${ISTA_MODULE}/timing_propagator/tp_data_manager
    ${ISTA_MODULE}/timing_propagator
)
