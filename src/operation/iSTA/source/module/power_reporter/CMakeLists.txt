add_library(ista_power_reporter
    ${ISTA_MODULE}/power_reporter/PowerReporter.cpp
)

target_link_libraries(ista_power_reporter
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_power_reporter
    PUBLIC
    ${ISTA_MODULE}/power_reporter/pr_data_manager
    ${ISTA_MODULE}/power_reporter
)
