
add_subdirectory(config)

add_library(flow
  flow.cpp
)

target_link_libraries(flow tool_manager flow_config ecc_tcl ecc_logger
# file_manager_drc
)
target_include_directories(flow
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/config

        ${HOME_PLATFORM}/tool_manager
        ${HOME_PLATFORM}/tool_manager/module
)

