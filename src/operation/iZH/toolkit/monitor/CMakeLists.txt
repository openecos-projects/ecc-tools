if(DEBUG_IZH_MONITOR)
    message(STATUS "ZH: DEBUG_IZH_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_monitor
    ${IZH_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(izh_monitor
    PUBLIC
    izh_toolkit
)

target_include_directories(izh_monitor
    PUBLIC
    ${IZH_TOOLKIT}/monitor
)
