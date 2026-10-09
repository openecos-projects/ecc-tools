add_library(ipw_monitor
    ${IPW_TOOLKIT}/monitor/Monitor.cpp
)

target_link_libraries(ipw_monitor
    PUBLIC
    ipw_data_manager
)

target_include_directories(ipw_monitor
    PUBLIC
    ${IPW_TOOLKIT}/monitor
)
