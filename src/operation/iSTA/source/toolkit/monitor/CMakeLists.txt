add_library(ista_monitor
    ${ISTA_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ista_monitor
    PUBLIC
    ista_utility ista_logger
)

target_include_directories(ista_monitor
    PUBLIC
    ${ISTA_TOOLKIT}/monitor
)
