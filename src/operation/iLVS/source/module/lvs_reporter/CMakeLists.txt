if(DEBUG_ILVS_LVS_REPORTER)
    message(STATUS "LVS: DEBUG_ILVS_LVS_REPORTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_LVS_REPORTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_lvs_reporter
    ## module lvs_reporter
    ${ILVS_MODULE}/lvs_reporter/LVSReporter.cpp
)

target_link_libraries(ilvs_lvs_reporter
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_lvs_reporter
    PUBLIC
    ${ILVS_MODULE}/lvs_reporter/lr_data_manager
    ${ILVS_MODULE}/lvs_reporter
)
