if(DEBUG_ILVS_MONITOR)
    message(STATUS "LVS: DEBUG_ILVS_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_monitor
    ${ILVS_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ilvs_monitor
    PUBLIC
    ilvs_data_manager
)

target_include_directories(ilvs_monitor
    PUBLIC
    ${ILVS_TOOLKIT}/monitor
)
