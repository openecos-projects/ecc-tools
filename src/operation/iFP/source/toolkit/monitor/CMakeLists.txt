if(DEBUG_IFP_MONITOR)
    message(STATUS "iFP: DEBUG_IFP_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_monitor
    ${IFP_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ifp_monitor
    PUBLIC
    ifp_data_manager
    ifp_logger
    ifp_utility
)

target_include_directories(ifp_monitor
    PUBLIC
    ${IFP_TOOLKIT}/monitor
)
