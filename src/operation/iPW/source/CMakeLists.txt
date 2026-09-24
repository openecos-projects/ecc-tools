## set path
set(IPW_DATA_MANAGER ${IPW_SOURCE}/data_manager)
set(IPW_MODULE ${IPW_SOURCE}/module)
set(IPW_TOOLKIT ${IPW_SOURCE}/toolkit)

## build
add_subdirectory(${IPW_DATA_MANAGER})
add_subdirectory(${IPW_MODULE})
add_subdirectory(${IPW_TOOLKIT})

add_library(ipw_source INTERFACE)

target_link_libraries(ipw_source
    INTERFACE
    ipw_data_manager
    ipw_module
    ipw_toolkit
)
