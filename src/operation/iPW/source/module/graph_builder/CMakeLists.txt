add_library(ipw_graph_builder
    ${IPW_MODULE}/graph_builder/GraphBuilder.cpp
)

target_link_libraries(ipw_graph_builder
    PUBLIC
    ipw_delay_calculator
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_graph_builder
    PUBLIC
    ${IPW_MODULE}/graph_builder/gb_data_manager
    ${IPW_MODULE}/graph_builder
)
