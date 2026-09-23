add_library(ipw_path_exception
    ${IPW_TOOLKIT}/path_exception/TimingExceptionMatcher.cpp
)

target_link_libraries(ipw_path_exception
    PUBLIC
    ipw_data_manager
    PRIVATE
    ipw_utility
)

target_include_directories(ipw_path_exception
    PUBLIC
    ${IPW_TOOLKIT}/path_exception
)
