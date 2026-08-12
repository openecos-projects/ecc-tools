if(DEBUG_IRT_TOPO_BUILDER)
    message(STATUS "RT: DEBUG_IRT_TOPO_BUILDER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "RT: RELEASE_IRT_TOPO_BUILDER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(irt_topo_builder
    ## module topo_builder
    ${IRT_MODULE}/topo_builder/TOPOBuilder.cpp
)

target_link_libraries(irt_topo_builder
    PUBLIC
    irt_data_manager
    irt_module
    irt_toolkit
    PRIVATE
    flute
)

target_include_directories(irt_topo_builder
    PUBLIC
    ${IRT_MODULE}/topo_builder/tb_data_manager
    ${IRT_MODULE}/topo_builder
)
