add_library(flow_config
        flow_config.cpp
)

target_include_directories(flow_config 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}/json
    ${HOME_THIRDPARTY}/json
)
