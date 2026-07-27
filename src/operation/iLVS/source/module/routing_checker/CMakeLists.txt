if(DEBUG_ILVS_ROUTING_CHECKER)
    message(STATUS "LVS: DEBUG_ILVS_ROUTING_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_ROUTING_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_routing_checker
    ## module routing_checker
    ${ILVS_MODULE}/routing_checker/RoutingChecker.cpp
)

target_link_libraries(ilvs_routing_checker
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_routing_checker
    PUBLIC
    ${ILVS_MODULE}/routing_checker/rc_data_manager
    ${ILVS_MODULE}/routing_checker
)
