if(DEBUG_ISTA_MONITOR)
    message(STATUS "iSTA: DEBUG_ISTA_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_monitor
    ${ISTA_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ista_monitor
    PUBLIC
    ista_data_manager
)

target_include_directories(ista_monitor
    PUBLIC
    ${ISTA_TOOLKIT}/monitor
)
