add_library(ipw_power_analyzer
    ${IPW_MODULE}/power_analyzer/PowerAnalyzer.cpp
)

target_link_libraries(ipw_power_analyzer
    PUBLIC
    ipw_data_manager
    ipw_delay_calculator
    ipw_toolkit
)

target_include_directories(ipw_power_analyzer
    PUBLIC
    ${IPW_MODULE}/power_analyzer/pa_data_manager
    ${IPW_MODULE}/power_analyzer
)
