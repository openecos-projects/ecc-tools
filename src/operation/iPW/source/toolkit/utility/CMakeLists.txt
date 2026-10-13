add_library(ipw_utility
    ${IPW_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ipw_utility
    PUBLIC
    ipw_data_manager
)

target_include_directories(ipw_utility
    PUBLIC
    ${IPW_TOOLKIT}/utility
)
