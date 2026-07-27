if(DEBUG_IEMIR_MONITOR)
    message(STATUS "iEMIR: DEBUG_IEMIR_MONITOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_MONITOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_monitor
    ${IEMIR_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(iemir_monitor
    PUBLIC
    iemir_data_manager
    iemir_logger
    iemir_utility
)

target_include_directories(iemir_monitor
    PUBLIC
    ${IEMIR_TOOLKIT}/monitor
)
