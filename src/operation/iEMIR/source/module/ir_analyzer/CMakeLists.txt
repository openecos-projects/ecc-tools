find_package(Eigen3 REQUIRED NO_MODULE)

if(DEBUG_IEMIR_IR_ANALYZER)
    message(STATUS "iEMIR: DEBUG_IEMIR_IR_ANALYZER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_IR_ANALYZER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_ir_analyzer
    ${IEMIR_MODULE}/ir_analyzer/IRAnalyzer.cpp
)

target_link_libraries(iemir_ir_analyzer
    PRIVATE
    Eigen3::Eigen
    PUBLIC
    iemir_data_manager
    iemir_toolkit
)

target_include_directories(iemir_ir_analyzer
    PUBLIC
    ${IEMIR_MODULE}/ir_analyzer/ia_data_manager
    ${IEMIR_MODULE}/ir_analyzer
)
