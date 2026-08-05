add_library(ista_timing_reporter
    ${ISTA_MODULE}/timing_reporter/TimingReporter.cpp
)

target_link_libraries(ista_timing_reporter
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_timing_reporter
    PUBLIC
    ${ISTA_MODULE}/timing_reporter/tr_data_manager
    ${ISTA_MODULE}/timing_reporter
)
