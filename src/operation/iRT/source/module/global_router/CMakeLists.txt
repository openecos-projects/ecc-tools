add_library(irt_global_router
    ${IRT_MODULE}/global_router/GlobalRouter.cpp
    ${IRT_MODULE}/global_router/GRGridGraph.cpp
    ${IRT_MODULE}/global_router/GRNet.cpp
    ${IRT_MODULE}/global_router/GRCost.cpp
    ${IRT_MODULE}/global_router/GRTreeBuilder.cpp
    ${IRT_MODULE}/global_router/GRMaze.cpp
)

target_link_libraries(irt_global_router
    PUBLIC
    irt_data_manager
    irt_module
    irt_toolkit
    PRIVATE
    flute
)

target_include_directories(irt_global_router
    PUBLIC
    ${IRT_MODULE}/global_router
)
