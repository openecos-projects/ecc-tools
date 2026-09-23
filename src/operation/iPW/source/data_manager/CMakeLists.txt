add_library(ipw_data_manager
    ${IPW_DATA_MANAGER}/DataManager.cpp
    ${IPW_DATA_MANAGER}/constraint/TimingCaseAnalysis.cpp
)

target_link_libraries(ipw_data_manager
    PUBLIC
    ipw_toolkit
    ipw_sdc_command
)

target_include_directories(ipw_data_manager
    PUBLIC
    ${IPW_DATA_MANAGER}/advance
    ${IPW_DATA_MANAGER}/basic
    ${IPW_DATA_MANAGER}
    ${IPW_DATA_MANAGER}/constraint
    ${IPW_MODULE}/sdc_command
)
