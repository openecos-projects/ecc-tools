add_library(ipw_power_propagator
    ${IPW_MODULE}/power_propagator/PowerActivityModel.cpp
    ${IPW_MODULE}/power_propagator/PowerVectorSimulator.cpp
    ${IPW_MODULE}/power_propagator/PowerPropagator.cpp
)

target_link_libraries(ipw_power_propagator
    PUBLIC
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_power_propagator
    PUBLIC
    ${IPW_MODULE}/power_propagator/pp_data_manager
    ${IPW_MODULE}/power_propagator
)
