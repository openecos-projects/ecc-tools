## set path
set(IMJ_DATA_MANAGER ${IMJ_SOURCE}/data_manager)
set(IMJ_MODULE ${IMJ_SOURCE}/module)
set(IMJ_TOOLKIT ${IMJ_SOURCE}/toolkit)

## build
add_subdirectory(${IMJ_DATA_MANAGER})
add_subdirectory(${IMJ_MODULE})
add_subdirectory(${IMJ_TOOLKIT})

add_library(imj_source INTERFACE)

target_link_libraries(imj_source
    INTERFACE
    imj_data_manager
    imj_module
    imj_toolkit
)
