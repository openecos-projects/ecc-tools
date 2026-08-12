## set path
set(ILVS_DATA_MANAGER ${ILVS_SOURCE}/data_manager)
set(ILVS_MODULE ${ILVS_SOURCE}/module)
set(ILVS_TOOLKIT ${ILVS_SOURCE}/toolkit)

## build
add_subdirectory(${ILVS_DATA_MANAGER})
add_subdirectory(${ILVS_MODULE})
add_subdirectory(${ILVS_TOOLKIT})

add_library(ilvs_source INTERFACE)

target_link_libraries(ilvs_source
    INTERFACE
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)
