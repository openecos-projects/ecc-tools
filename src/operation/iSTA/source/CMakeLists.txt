## set path
set(ISTA_DATA_MANAGER ${ISTA_SOURCE}/data_manager)
set(ISTA_MODULE ${ISTA_SOURCE}/module)
set(ISTA_TOOLKIT ${ISTA_SOURCE}/toolkit)

## build
add_subdirectory(${ISTA_DATA_MANAGER})
add_subdirectory(${ISTA_MODULE})
add_subdirectory(${ISTA_TOOLKIT})

add_library(ista_source INTERFACE)

target_link_libraries(ista_source
    INTERFACE
    ista_data_manager
    ista_module
    ista_toolkit
)
