if(DEBUG_IRCX_MONITOR)
    message(STATUS "iRCX: DEBUG_IRCX_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_monitor
    ${IRCX_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ircx_monitor
    PUBLIC
    ircx_data_manager
)

target_include_directories(ircx_monitor
    PUBLIC
    ${IRCX_TOOLKIT}/monitor
)
