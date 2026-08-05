set(IFP_DATA_MANAGER ${IFP_SOURCE}/data_manager)
set(IFP_MODULE ${IFP_SOURCE}/module)
set(IFP_TOOLKIT ${IFP_SOURCE}/toolkit)

add_subdirectory(${IFP_DATA_MANAGER})
add_subdirectory(${IFP_MODULE})
add_subdirectory(${IFP_TOOLKIT})

add_library(ifp_source INTERFACE)

target_link_libraries(ifp_source
    INTERFACE
    ifp_data_manager
    ifp_module
    ifp_toolkit
)
