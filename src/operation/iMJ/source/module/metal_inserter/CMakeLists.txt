if(DEBUG_IMJ_METAL_INSERTER)
    message(STATUS "MJ: DEBUG_IMJ_METAL_INSERTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_METAL_INSERTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_metal_inserter
    ## module metal_inserter
    ${IMJ_MODULE}/metal_inserter/MetalInserter.cpp
)

target_link_libraries(imj_metal_inserter
    PUBLIC
    imj_module
    imj_toolkit
    idm
    geometry_builder
)

target_include_directories(imj_metal_inserter
    PUBLIC
    ${IMJ_MODULE}/metal_inserter/mi_data_manager
    ${IMJ_MODULE}/metal_inserter
)
