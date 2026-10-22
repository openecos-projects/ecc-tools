add_library(irt_global_router
    ${IRT_MODULE}/global_router/GlobalRouter.cpp
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
    ${IRT_MODULE}/global_router/gr_data_manager
    ${IRT_MODULE}/global_router
)
