if(DEBUG_ISTA_POWER_PROPAGATOR)
    message(STATUS "iSTA: DEBUG_ISTA_POWER_PROPAGATOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_POWER_PROPAGATOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_power_propagator
    ${ISTA_MODULE}/power_propagator/PowerPropagator.cpp
)

target_link_libraries(ista_power_propagator
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_power_propagator
    PUBLIC
    ${ISTA_MODULE}/power_propagator/pp_data_manager
    ${ISTA_MODULE}/power_propagator
)
