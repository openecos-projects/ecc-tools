if(DEBUG_ISTA_TIMING_REPORTER)
    message(STATUS "iSTA: DEBUG_ISTA_TIMING_REPORTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_TIMING_REPORTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

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
