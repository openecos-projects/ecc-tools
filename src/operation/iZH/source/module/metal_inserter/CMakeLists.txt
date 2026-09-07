if(DEBUG_IZH_METAL_INSERTER)
    message(STATUS "ZH: DEBUG_IZH_METAL_INSERTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_METAL_INSERTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_metal_inserter
    ## module metal_inserter
    ${IZH_MODULE}/metal_inserter/MetalInserter.cpp
)

target_link_libraries(izh_metal_inserter
    PUBLIC
    izh_module
    izh_toolkit
    idm
    geometry_builder
)

target_include_directories(izh_metal_inserter
    PUBLIC
    ${IZH_MODULE}/metal_inserter/mi_data_manager
    ${IZH_MODULE}/metal_inserter
)
