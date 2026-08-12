if(DEBUG_ISTA_CLOCK_PROPAGATOR)
    message(STATUS "iSTA: DEBUG_ISTA_CLOCK_PROPAGATOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_CLOCK_PROPAGATOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_clock_propagator
    ${ISTA_MODULE}/clock_propagator/ClockPropagator.cpp
)

target_link_libraries(ista_clock_propagator
    PUBLIC
    ista_delay_calculator
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_clock_propagator
    PUBLIC
    ${ISTA_MODULE}/clock_propagator/cp_data_manager
    ${ISTA_MODULE}/clock_propagator
)
