if(DEBUG_IEMIR_GRAPH_BUILDER)
    message(STATUS "iEMIR: DEBUG_IEMIR_GRAPH_BUILDER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_GRAPH_BUILDER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_graph_builder
    ${IEMIR_MODULE}/graph_builder/GraphBuilder.cpp
)

target_link_libraries(iemir_graph_builder
    PUBLIC
    iemir_data_manager
    iemir_toolkit
)

target_include_directories(iemir_graph_builder
    PUBLIC
    ${IEMIR_MODULE}/graph_builder/gb_data_manager
    ${IEMIR_MODULE}/graph_builder
)
