## set path
set(IRCX_DATA_MANAGER ${IRCX_SOURCE}/data_manager)
set(IRCX_MODULE ${IRCX_SOURCE}/module)
set(IRCX_TOOLKIT ${IRCX_SOURCE}/toolkit)

## build
add_subdirectory(${IRCX_DATA_MANAGER})
add_subdirectory(${IRCX_MODULE})
add_subdirectory(${IRCX_TOOLKIT})

add_library(ircx_source INTERFACE)

target_link_libraries(ircx_source
    INTERFACE
    ircx_data_manager
    ircx_module
    ircx_toolkit
)
