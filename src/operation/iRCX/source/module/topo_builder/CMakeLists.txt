if(DEBUG_IRCX_TOPO_BUILDER)
    message(STATUS "iRCX: DEBUG_IRCX_TOPO_BUILDER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_TOPO_BUILDER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_topo_builder
    ${IRCX_MODULE}/topo_builder/TopoBuilder.cpp
)

target_link_libraries(ircx_topo_builder
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_topo_builder
    PUBLIC
    ${IRCX_MODULE}/topo_builder/tb_data_manager
    ${IRCX_MODULE}/topo_builder
)
