if(DEBUG_IMJ_MONITOR)
    message(STATUS "MJ: DEBUG_IMJ_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_monitor
    ${IMJ_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(imj_monitor
    PUBLIC
    imj_toolkit
)

target_include_directories(imj_monitor
    PUBLIC
    ${IMJ_TOOLKIT}/monitor
)
