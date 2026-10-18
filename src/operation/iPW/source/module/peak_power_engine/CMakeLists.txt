add_library(ipw_peak_power_engine
    ${IPW_MODULE}/peak_power_engine/PeakPowerEngine.cpp
)

target_link_libraries(ipw_peak_power_engine
    PUBLIC
    ipw_data_manager
    ipw_toolkit
    ipw_power_propagator
    ipw_power_analyzer
    vcd
)

target_include_directories(ipw_peak_power_engine
    PUBLIC
    ${IPW_MODULE}/peak_power_engine
)
