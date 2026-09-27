add_library(ipw_logger
    ${IPW_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ipw_logger
    PUBLIC
    ipw_data_manager
)

target_include_directories(ipw_logger
    PUBLIC
    ${IPW_TOOLKIT}/logger
)
