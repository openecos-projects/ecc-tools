if(DEBUG_IEMIR_EMIR_REPORTER)
    message(STATUS "iEMIR: DEBUG_IEMIR_EMIR_REPORTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_EMIR_REPORTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_emir_reporter
    ${IEMIR_MODULE}/emir_reporter/EMIRReporter.cpp
)

target_link_libraries(iemir_emir_reporter
    PUBLIC
    iemir_data_manager
    iemir_toolkit
)

target_include_directories(iemir_emir_reporter
    PUBLIC
    ${IEMIR_MODULE}/emir_reporter/er_data_manager
    ${IEMIR_MODULE}/emir_reporter
)
