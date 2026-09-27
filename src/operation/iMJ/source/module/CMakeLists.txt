add_subdirectory(${IMJ_MODULE}/antenna_checker)
add_subdirectory(${IMJ_MODULE}/def_flattener)
add_subdirectory(${IMJ_MODULE}/filler_inserter)
add_subdirectory(${IMJ_MODULE}/metal_inserter)

add_library(imj_module INTERFACE)

target_link_libraries(imj_module
    INTERFACE
    imj_antenna_checker
    imj_def_flattener
    imj_filler_inserter
    imj_metal_inserter
)
