if(DEBUG_ISTA_POWER_REPORTER)
    message(STATUS "iSTA: DEBUG_ISTA_POWER_REPORTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_POWER_REPORTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

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
