find_package(Eigen3 REQUIRED NO_MODULE)

if(DEBUG_ISTA_DELAY_CALCULATOR)
    message(STATUS "iSTA: DEBUG_ISTA_DELAY_CALCULATOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_DELAY_CALCULATOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_delay_calculator
    ${ISTA_MODULE}/delay_calculator/DelayCalculator.cpp
)

target_link_libraries(ista_delay_calculator
    PRIVATE
    Eigen3::Eigen
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_delay_calculator
    PUBLIC
    ${ISTA_MODULE}/delay_calculator/dc_data_manager
    ${ISTA_MODULE}/delay_calculator
)
