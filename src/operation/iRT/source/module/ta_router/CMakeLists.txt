add_library(irt_ta_router
    ${IRT_MODULE}/ta_router/TARouter.cpp
    ${IRT_MODULE}/ta_router/TATrackTable.cpp
)

target_link_libraries(irt_ta_router
    PUBLIC
    irt_data_manager
    irt_module
    irt_toolkit
)

target_include_directories(irt_ta_router
    PUBLIC
    ${IRT_MODULE}/ta_router
)
