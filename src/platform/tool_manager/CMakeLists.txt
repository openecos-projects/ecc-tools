add_library(tool_manager
    tool_manager.cpp
)

target_link_libraries(tool_manager PUBLIC idm)

target_include_directories(tool_manager
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_PLATFORM}/
    ${HOME_PLATFORM}/data_manager
    ${HOME_PLATFORM}/data_manager/config
    ${HOME_PLATFORM}/flow/config
    ${HOME_UTILITY}/json
    ${HOME_OPERATION}
)
