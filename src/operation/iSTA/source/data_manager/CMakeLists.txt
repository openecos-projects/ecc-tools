add_library(ista_data_manager
    ${ISTA_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ista_data_manager
    PUBLIC
    ista_toolkit
    ista_sdc_command
)

target_include_directories(ista_data_manager
    PUBLIC
    ${ISTA_DATA_MANAGER}/advance
    ${ISTA_DATA_MANAGER}/basic
    ${ISTA_DATA_MANAGER}
    ${ISTA_MODULE}/sdc_command/sdc_commands
)
