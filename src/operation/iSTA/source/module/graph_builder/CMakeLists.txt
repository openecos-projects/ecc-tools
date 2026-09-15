add_library(ista_graph_builder
    ${ISTA_MODULE}/graph_builder/GraphBuilder.cpp
)

target_link_libraries(ista_graph_builder
    PUBLIC
    ista_delay_calculator
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_graph_builder
    PUBLIC
    ${ISTA_MODULE}/graph_builder/gb_data_manager
    ${ISTA_MODULE}/graph_builder
)
