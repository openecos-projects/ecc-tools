if(DEBUG_ISTA_POWER_ANALYZER)
    message(STATUS "iSTA: DEBUG_ISTA_POWER_ANALYZER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_POWER_ANALYZER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_power_analyzer
    ${ISTA_MODULE}/power_analyzer/PowerAnalyzer.cpp
)

target_link_libraries(ista_power_analyzer
    PUBLIC
    ista_data_manager
    ista_delay_calculator
    ista_toolkit
)

target_include_directories(ista_power_analyzer
    PUBLIC
    ${ISTA_MODULE}/power_analyzer/pa_data_manager
    ${ISTA_MODULE}/power_analyzer
)
