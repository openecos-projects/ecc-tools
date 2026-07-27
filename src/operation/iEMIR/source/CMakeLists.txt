set(IEMIR_DATA_MANAGER ${IEMIR_SOURCE}/data_manager)
set(IEMIR_MODULE ${IEMIR_SOURCE}/module)
set(IEMIR_TOOLKIT ${IEMIR_SOURCE}/toolkit)

add_subdirectory(${IEMIR_DATA_MANAGER})
add_subdirectory(${IEMIR_MODULE})
add_subdirectory(${IEMIR_TOOLKIT})

add_library(iemir_source INTERFACE)

target_link_libraries(iemir_source
    INTERFACE
    iemir_data_manager
    iemir_module
    iemir_toolkit
)
