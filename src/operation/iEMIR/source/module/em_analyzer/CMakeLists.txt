if(DEBUG_IEMIR_EM_ANALYZER)
    message(STATUS "iEMIR: DEBUG_IEMIR_EM_ANALYZER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_EM_ANALYZER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_em_analyzer
    ${IEMIR_MODULE}/em_analyzer/EMAnalyzer.cpp
)

target_link_libraries(iemir_em_analyzer
    PUBLIC
    iemir_data_manager
    iemir_toolkit
)

target_include_directories(iemir_em_analyzer
    PUBLIC
    ${IEMIR_MODULE}/em_analyzer/ea_data_manager
    ${IEMIR_MODULE}/em_analyzer
)
